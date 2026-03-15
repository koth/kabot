#include "relay/relay_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <openssl/ssl.h>

#include "nlohmann/json.hpp"
#include "utils/logging.hpp"

namespace kabot::relay {
namespace {

using Json = nlohmann::json;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

std::string UrlEncode(const std::string& value) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex << std::uppercase;
    for (const auto ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (std::isalnum(byte) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded << ch;
        } else {
            encoded << '%' << std::setw(2) << static_cast<int>(byte);
        }
    }
    return encoded.str();
}

std::string IsoNow() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string BuildTarget(const kabot::config::RelayManagedAgentConfig& config) {
    std::ostringstream oss;
    oss << config.path
        << "?agentId=" << UrlEncode(config.agent_id)
        << "&token=" << UrlEncode(config.token);
    return oss.str();
}

std::string BuildDailySummaryTarget(const kabot::config::RelayManagedAgentConfig& config) {
    return "/api/agents/" + UrlEncode(config.agent_id) + "/daily-summary";
}

std::string BuildClaimNextTaskTarget(const kabot::config::RelayManagedAgentConfig& config) {
    return "/api/agents/" + UrlEncode(config.agent_id) + "/tasks/claim-next";
}

std::string BuildTaskStatusTarget(const kabot::config::RelayManagedAgentConfig& config,
                                  const std::string& task_id) {
    return "/api/agents/" + UrlEncode(config.agent_id) + "/tasks/" + UrlEncode(task_id) + "/status";
}

std::string BuildSessionKey(const kabot::config::RelayManagedAgentConfig& config,
                            const std::string& command_id) {
    return "relay:" + config.name + ":" + command_id;
}

std::string Trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool IsValidSummaryDate(const std::string& value) {
    static const std::regex pattern(R"(^\d{4}-\d{2}-\d{2}$)");
    if (!std::regex_match(value, pattern)) {
        return false;
    }
    int year = 0;
    int month = 0;
    int day = 0;
    if (std::sscanf(value.c_str(), "%d-%d-%d", &year, &month, &day) != 3) {
        return false;
    }
    if (month < 1 || month > 12) {
        return false;
    }
    static const int days_per_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int max_day = days_per_month[month - 1];
    const bool leap_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (month == 2 && leap_year) {
        max_day = 29;
    }
    return day >= 1 && day <= max_day;
}

bool IsIpLiteral(const std::string& host) {
    boost::system::error_code ec;
    net::ip::make_address(host, ec);
    return !ec;
}

RelayTask ParseRelayTask(const Json& json) {
    RelayTask task{};
    if (!json.is_object()) {
        return task;
    }
    task.task_id = json.value("taskId", std::string());
    task.title = json.value("title", std::string());
    task.instruction = json.value("instruction", std::string());
    task.session_key = json.value("sessionKey", std::string());
    task.created_at = json.value("createdAt", std::string());
    task.priority = json.value("priority", std::string());
    if (json.contains("interaction") && json["interaction"].is_object()) {
        const auto& interaction = json["interaction"];
        task.interaction.channel = interaction.value("channel", std::string());
        task.interaction.channel_instance = interaction.value("channelInstance", std::string());
        task.interaction.chat_id = interaction.value("chatId", std::string());
        task.interaction.reply_to = interaction.value("replyTo", std::string());
    }
    if (json.contains("metadata") && json["metadata"].is_object()) {
        for (const auto& [key, value] : json["metadata"].items()) {
            if (value.is_string()) {
                task.metadata[key] = value.get<std::string>();
            } else if (!value.is_null()) {
                task.metadata[key] = value.dump();
            }
        }
    }
    return task;
}

template <typename Response>
std::string DescribeHandshakeResponse(const Response& response) {
    std::ostringstream oss;
    oss << "http_status=" << response.result_int() << " reason=" << response.reason();
    if (!response.body().empty()) {
        oss << " body=" << response.body();
    }
    return oss.str();
}

class IWebSocketSession {
public:
    virtual ~IWebSocketSession() = default;
    virtual void Connect() = 0;
    virtual void Close() = 0;
    virtual void WriteText(const std::string& payload) = 0;
    virtual std::string ReadText() = 0;
};

class PlainWebSocketSession final : public IWebSocketSession {
public:
    explicit PlainWebSocketSession(const kabot::config::RelayManagedAgentConfig& config)
        : config_(config)
        , resolver_(ioc_)
        , ws_(ioc_) {}

    void Connect() override {
        const auto port = std::to_string(config_.port);
        tcp::resolver::results_type endpoints;
        try {
            endpoints = resolver_.resolve(config_.host, port);
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string("resolve failed: ") + ex.what());
        }

        tcp::endpoint endpoint;
        try {
            endpoint = net::connect(ws_.next_layer(), endpoints);
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string("tcp connect failed: ") + ex.what());
        }

        auto host = config_.host + ":" + std::to_string(endpoint.port());
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws_.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
            req.set(http::field::user_agent, "kabot-relay/1.0");
        }));

        http::response<http::string_body> response;
        beast::error_code ec;
        ws_.handshake(response, host, BuildTarget(config_), ec);
        if (ec) {
            throw std::runtime_error(std::string("websocket handshake failed: ")
                                     + ec.message() + " (" + DescribeHandshakeResponse(response) + ")");
        }
        ws_.text(true);
    }

    void Close() override {
        beast::error_code ec;
        ws_.close(websocket::close_code::normal, ec);
    }

    void WriteText(const std::string& payload) override {
        ws_.write(net::buffer(payload));
    }

    std::string ReadText() override {
        beast::flat_buffer buffer;
        ws_.read(buffer);
        return beast::buffers_to_string(buffer.data());
    }

private:
    kabot::config::RelayManagedAgentConfig config_;
    net::io_context ioc_;
    tcp::resolver resolver_;
    websocket::stream<tcp::socket> ws_;
};

class TlsWebSocketSession final : public IWebSocketSession {
public:
    explicit TlsWebSocketSession(const kabot::config::RelayManagedAgentConfig& config)
        : config_(config)
        , resolver_(ioc_)
        , ssl_ctx_(ssl::context::tls_client)
        , ws_(ioc_, ssl_ctx_) {
        ssl_ctx_.set_default_verify_paths();
        ssl_ctx_.set_verify_mode(config_.skip_tls_verify ? ssl::verify_none : ssl::verify_peer);
    }

    void Connect() override {
        const auto port = std::to_string(config_.port);
        tcp::resolver::results_type endpoints;
        try {
            endpoints = resolver_.resolve(config_.host, port);
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string("resolve failed: ") + ex.what());
        }

        tcp::endpoint endpoint;
        try {
            endpoint = beast::get_lowest_layer(ws_).connect(endpoints);
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string("tcp connect failed: ") + ex.what());
        }

        auto host = config_.host + ":" + std::to_string(endpoint.port());
        if (!IsIpLiteral(config_.host)) {
            if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), config_.host.c_str())) {
                throw std::runtime_error("failed to set TLS SNI host");
            }
        }
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws_.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
            req.set(http::field::user_agent, "kabot-relay/1.0");
        }));
        try {
            ws_.next_layer().handshake(ssl::stream_base::client);
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string("tls handshake failed: ") + ex.what());
        }

        http::response<http::string_body> response;
        beast::error_code ec;
        ws_.handshake(response, host, BuildTarget(config_), ec);
        if (ec) {
            throw std::runtime_error(std::string("websocket handshake failed: ")
                                     + ec.message() + " (" + DescribeHandshakeResponse(response) + ")");
        }
        ws_.text(true);
    }

    void Close() override {
        beast::error_code ec;
        ws_.close(websocket::close_code::normal, ec);
        ws_.next_layer().shutdown(ec);
    }

    void WriteText(const std::string& payload) override {
        ws_.write(net::buffer(payload));
    }

    std::string ReadText() override {
        beast::flat_buffer buffer;
        ws_.read(buffer);
        return beast::buffers_to_string(buffer.data());
    }

private:
    kabot::config::RelayManagedAgentConfig config_;
    net::io_context ioc_;
    tcp::resolver resolver_;
    ssl::context ssl_ctx_;
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws_;
};

std::shared_ptr<IWebSocketSession> CreateSession(const kabot::config::RelayManagedAgentConfig& config) {
    if (config.scheme == "wss" || config.use_tls) {
        return std::make_shared<TlsWebSocketSession>(config);
    }
    return std::make_shared<PlainWebSocketSession>(config);
}

Json BuildActivityPayload(const std::string& status,
                          const std::string& summary,
                          const std::string& command_id = {}) {
    Json payload = {
        {"type", "activity.update"},
        {"activityStatus", status},
        {"activitySummary", summary},
        {"reportedAt", IsoNow()}
    };
    if (!command_id.empty()) {
        payload["currentCommandId"] = command_id;
    }
    return payload;
}

std::string PhaseActivitySummary(kabot::agent::DirectExecutionPhase phase) {
    switch (phase) {
    case kabot::agent::DirectExecutionPhase::kReceived:
        return "Received relay command";
    case kabot::agent::DirectExecutionPhase::kProcessing:
        return "Processing relay command";
    case kabot::agent::DirectExecutionPhase::kCallingTools:
        return "Calling tools";
    case kabot::agent::DirectExecutionPhase::kReplying:
        return "Preparing reply";
    }
    return "Executing relay command";
}

Json BuildAckPayload(const std::string& command_id) {
    return {
        {"type", "command.ack"},
        {"commandId", command_id}
    };
}

Json BuildResultPayload(const std::string& command_id,
                        const std::string& status,
                        const std::string& field_name,
                        const std::string& field_value,
                        std::optional<int> progress = std::nullopt) {
    Json payload = {
        {"type", "command.result"},
        {"commandId", command_id},
        {"status", status}
    };
    if (!field_name.empty()) {
        payload[field_name] = field_value;
    }
    if (progress.has_value()) {
        payload["progress"] = *progress;
    }
    return payload;
}

template <typename Stream>
http::response<http::string_body> SendDailySummaryRequest(
    Stream& stream,
    const kabot::config::RelayManagedAgentConfig& config,
    const std::string& summary_date,
    const std::string& content,
    const std::string& reported_at) {
    Json body = {
        {"summaryDate", summary_date},
        {"content", content}
    };
    if (!reported_at.empty()) {
        body["reportedAt"] = reported_at;
    }

    http::request<http::string_body> request{http::verb::post, BuildDailySummaryTarget(config), 11};
    request.set(http::field::host, config.host);
    request.set(http::field::user_agent, "kabot-relay/1.0");
    request.set(http::field::authorization, "Bearer " + config.token);
    request.set(http::field::content_type, "application/json");
    request.body() = body.dump();
    request.prepare_payload();

    http::write(stream, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);
    return response;
}

template <typename Stream>
http::response<http::string_body> SendClaimNextTaskRequest(
    Stream& stream,
    const kabot::config::RelayManagedAgentConfig& config,
    bool supports_interaction) {
    Json body = {
        {"localAgent", config.local_agent},
        {"claimedAt", IsoNow()},
        {"workerId", "gateway:" + config.local_agent},
        {"supportsInteraction", supports_interaction}
    };

    http::request<http::string_body> request{http::verb::post, BuildClaimNextTaskTarget(config), 11};
    request.set(http::field::host, config.host);
    request.set(http::field::user_agent, "kabot-relay/1.0");
    request.set(http::field::authorization, "Bearer " + config.token);
    request.set(http::field::content_type, "application/json");
    request.body() = body.dump();
    request.prepare_payload();

    http::write(stream, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);
    return response;
}

template <typename Stream>
http::response<http::string_body> SendTaskStatusRequest(
    Stream& stream,
    const kabot::config::RelayManagedAgentConfig& config,
    const std::string& task_id,
    const RelayTaskStatusUpdate& update) {
    Json body = {
        {"status", update.status},
        {"reportedAt", update.reported_at.empty() ? IsoNow() : update.reported_at}
    };
    if (!update.message.empty()) {
        body["message"] = update.message;
    }
    if (update.progress >= 0) {
        body["progress"] = update.progress;
    }
    if (!update.session_key.empty()) {
        body["sessionKey"] = update.session_key;
    }
    if (!update.result.empty()) {
        body["result"] = update.result;
    }
    if (!update.error.empty()) {
        body["error"] = update.error;
    }
    if (!update.waiting_user.chat_id.empty()) {
        body["waitingUser"] = {
            {"channel", update.waiting_user.channel},
            {"channelInstance", update.waiting_user.channel_instance},
            {"chatId", update.waiting_user.chat_id},
            {"replyTo", update.waiting_user.reply_to}
        };
    }

    http::request<http::string_body> request{http::verb::post, BuildTaskStatusTarget(config, task_id), 11};
    request.set(http::field::host, config.host);
    request.set(http::field::user_agent, "kabot-relay/1.0");
    request.set(http::field::authorization, "Bearer " + config.token);
    request.set(http::field::content_type, "application/json");
    request.body() = body.dump();
    request.prepare_payload();

    http::write(stream, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);
    return response;
}

}  // namespace

class RelayManager::Worker {
public:
    Worker(kabot::config::RelayManagedAgentConfig config,
           kabot::agent::AgentRegistry& agents)
        : config_(std::move(config))
        , agents_(agents) {}

    ~Worker() {
        Stop();
    }

    void Start() {
        if (running_ || !config_.enabled) {
            return;
        }
        running_ = true;
        worker_ = std::thread([this] { Run(); });
    }

    void Stop() {
        running_ = false;
        StopHeartbeat();
        reconnect_cv_.notify_all();
        CloseSession();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void ReportInboundPhase(kabot::agent::DirectExecutionPhase phase) {
        try {
            SendJson(BuildActivityPayload("busy", kabot::agent::DirectExecutionPhaseSummary(phase)));
        } catch (const std::exception& ex) {
            LOG_WARN("[relay] worker={} inbound phase report failed: {}", config_.name, ex.what());
        } catch (...) {
            LOG_WARN("[relay] worker={} inbound phase report failed", config_.name);
        }
    }

    void ReportInboundCompletion(bool success, const std::string& summary) {
        try {
            if (success) {
                SendJson(BuildActivityPayload("idle", "Idle"));
            } else {
                SendJson(BuildActivityPayload("error",
                                              summary.empty() ? "channel execution failed" : summary));
            }
        } catch (const std::exception& ex) {
            LOG_WARN("[relay] worker={} inbound completion report failed: {}", config_.name, ex.what());
        } catch (...) {
            LOG_WARN("[relay] worker={} inbound completion report failed", config_.name);
        }
    }

    DailySummaryUploadResult UploadDailySummary(const std::string& summary_date,
                                                const std::string& content,
                                                const std::string& reported_at) const {
        const auto trimmed_date = Trim(summary_date);
        const auto trimmed_content = Trim(content);
        const auto trimmed_reported_at = Trim(reported_at);

        if (!config_.enabled) {
            return {false, 0, "relay managed agent is disabled"};
        }
        if (trimmed_date.empty() || !IsValidSummaryDate(trimmed_date)) {
            return {false, 0, "summaryDate must be in YYYY-MM-DD format"};
        }
        if (trimmed_content.empty()) {
            return {false, 0, "content is required"};
        }
        if (trimmed_content.size() > 4000) {
            return {false, 0, "content must be at most 4000 characters"};
        }

        try {
            const auto response = config_.use_tls || config_.scheme == "wss"
                ? UploadDailySummaryTls(trimmed_date, trimmed_content, trimmed_reported_at)
                : UploadDailySummaryPlain(trimmed_date, trimmed_content, trimmed_reported_at);
            const auto success = response.result_int() >= 200 && response.result_int() < 300;
            std::string message;
            if (!success) {
                message = !response.body().empty()
                    ? response.body()
                    : std::string(response.reason());
            }
            return {success, static_cast<int>(response.result_int()), message};
        } catch (const std::exception& ex) {
            return {false, 0, ex.what()};
        } catch (...) {
            return {false, 0, "unknown daily summary upload error"};
        }
    }

    RelayTaskClaimResult ClaimNextTask(bool supports_interaction) const {
        if (!config_.enabled) {
            return {false, false, 0, "relay managed agent is disabled", {}};
        }

        try {
            const auto response = config_.use_tls || config_.scheme == "wss"
                ? ClaimNextTaskTls(supports_interaction)
                : ClaimNextTaskPlain(supports_interaction);
            const auto http_status = static_cast<int>(response.result_int());
            if (http_status < 200 || http_status >= 300) {
                return {false, false, http_status,
                        !response.body().empty() ? response.body() : std::string(response.reason()),
                        {}};
            }

            auto json = Json::parse(response.body(), nullptr, false);
            if (json.is_discarded() || !json.is_object()) {
                return {false, false, http_status, "invalid claim-next response body", {}};
            }
            const auto found = json.value("found", false);
            if (!found) {
                return {true, false, http_status, {}, {}};
            }
            if (!json.contains("task") || !json["task"].is_object()) {
                return {false, false, http_status, "claim-next response missing task", {}};
            }
            return {true, true, http_status, {}, ParseRelayTask(json["task"])};
        } catch (const std::exception& ex) {
            return {false, false, 0, ex.what(), {}};
        } catch (...) {
            return {false, false, 0, "unknown claim-next error", {}};
        }
    }

    RelayTaskStatusUpdateResult UpdateTaskStatus(const std::string& task_id,
                                                 const RelayTaskStatusUpdate& update) const {
        const auto trimmed_task_id = Trim(task_id);
        if (!config_.enabled) {
            return {false, 0, "relay managed agent is disabled"};
        }
        if (trimmed_task_id.empty()) {
            return {false, 0, "task_id is required"};
        }
        if (update.status.empty()) {
            return {false, 0, "status is required"};
        }

        try {
            const auto response = config_.use_tls || config_.scheme == "wss"
                ? UpdateTaskStatusTls(trimmed_task_id, update)
                : UpdateTaskStatusPlain(trimmed_task_id, update);
            const auto success = response.result_int() >= 200 && response.result_int() < 300;
            std::string message;
            if (!success) {
                message = !response.body().empty()
                    ? response.body()
                    : std::string(response.reason());
            }
            return {success, static_cast<int>(response.result_int()), message};
        } catch (const std::exception& ex) {
            return {false, 0, ex.what()};
        } catch (...) {
            return {false, 0, "unknown task status update error"};
        }
    }

private:
    http::response<http::string_body> ClaimNextTaskPlain(bool supports_interaction) const {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        auto endpoints = resolver.resolve(config_.host, std::to_string(config_.port));
        beast::tcp_stream stream(ioc);
        stream.connect(endpoints);
        auto response = SendClaimNextTaskRequest(stream, config_, supports_interaction);
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return response;
    }

    http::response<http::string_body> ClaimNextTaskTls(bool supports_interaction) const {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        ssl::context ssl_ctx(ssl::context::tls_client);
        ssl_ctx.set_default_verify_paths();
        ssl_ctx.set_verify_mode(config_.skip_tls_verify ? ssl::verify_none : ssl::verify_peer);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);
        if (!IsIpLiteral(config_.host)) {
            if (!SSL_set_tlsext_host_name(stream.native_handle(), config_.host.c_str())) {
                throw std::runtime_error("failed to set TLS SNI host");
            }
        }
        auto endpoints = resolver.resolve(config_.host, std::to_string(config_.port));
        beast::get_lowest_layer(stream).connect(endpoints);
        stream.handshake(ssl::stream_base::client);
        auto response = SendClaimNextTaskRequest(stream, config_, supports_interaction);
        beast::error_code ec;
        stream.shutdown(ec);
        return response;
    }

    http::response<http::string_body> UpdateTaskStatusPlain(const std::string& task_id,
                                                            const RelayTaskStatusUpdate& update) const {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        auto endpoints = resolver.resolve(config_.host, std::to_string(config_.port));
        beast::tcp_stream stream(ioc);
        stream.connect(endpoints);
        auto response = SendTaskStatusRequest(stream, config_, task_id, update);
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return response;
    }

    http::response<http::string_body> UpdateTaskStatusTls(const std::string& task_id,
                                                          const RelayTaskStatusUpdate& update) const {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        ssl::context ssl_ctx(ssl::context::tls_client);
        ssl_ctx.set_default_verify_paths();
        ssl_ctx.set_verify_mode(config_.skip_tls_verify ? ssl::verify_none : ssl::verify_peer);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);
        if (!IsIpLiteral(config_.host)) {
            if (!SSL_set_tlsext_host_name(stream.native_handle(), config_.host.c_str())) {
                throw std::runtime_error("failed to set TLS SNI host");
            }
        }
        auto endpoints = resolver.resolve(config_.host, std::to_string(config_.port));
        beast::get_lowest_layer(stream).connect(endpoints);
        stream.handshake(ssl::stream_base::client);
        auto response = SendTaskStatusRequest(stream, config_, task_id, update);
        beast::error_code ec;
        stream.shutdown(ec);
        return response;
    }

    http::response<http::string_body> UploadDailySummaryPlain(const std::string& summary_date,
                                                              const std::string& content,
                                                              const std::string& reported_at) const {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        auto endpoints = resolver.resolve(config_.host, std::to_string(config_.port));
        beast::tcp_stream stream(ioc);
        stream.connect(endpoints);
        auto response = SendDailySummaryRequest(stream,
                                                config_,
                                                summary_date,
                                                content,
                                                reported_at.empty() ? IsoNow() : reported_at);
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return response;
    }

    http::response<http::string_body> UploadDailySummaryTls(const std::string& summary_date,
                                                            const std::string& content,
                                                            const std::string& reported_at) const {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        ssl::context ssl_ctx(ssl::context::tls_client);
        ssl_ctx.set_default_verify_paths();
        ssl_ctx.set_verify_mode(config_.skip_tls_verify ? ssl::verify_none : ssl::verify_peer);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);
        if (!IsIpLiteral(config_.host)) {
            if (!SSL_set_tlsext_host_name(stream.native_handle(), config_.host.c_str())) {
                throw std::runtime_error("failed to set TLS SNI host");
            }
        }
        auto endpoints = resolver.resolve(config_.host, std::to_string(config_.port));
        beast::get_lowest_layer(stream).connect(endpoints);
        stream.handshake(ssl::stream_base::client);
        auto response = SendDailySummaryRequest(stream,
                                                config_,
                                                summary_date,
                                                content,
                                                reported_at.empty() ? IsoNow() : reported_at);
        beast::error_code ec;
        stream.shutdown(ec);
        return response;
    }

    void Run() {
        auto reconnect_delay_ms = std::max(100, config_.reconnect_initial_delay_ms);
        while (running_) {
            try {
                Connect();
                reconnect_delay_ms = std::max(100, config_.reconnect_initial_delay_ms);
                ReadLoop();
            } catch (const std::exception& ex) {
                LOG_WARN("[relay] worker={} connection loop failed: {}", config_.name, ex.what());
            } catch (...) {
                LOG_WARN("[relay] worker={} connection loop failed", config_.name);
            }

            StopHeartbeat();
            CloseSession();
            if (!running_) {
                break;
            }
            LOG_INFO("[relay] worker={} reconnecting in {} ms", config_.name, reconnect_delay_ms);
            std::unique_lock<std::mutex> reconnect_lock(reconnect_mutex_);
            reconnect_cv_.wait_for(
                reconnect_lock,
                std::chrono::milliseconds(reconnect_delay_ms),
                [this] { return !running_; });
            reconnect_delay_ms = std::min(config_.reconnect_max_delay_ms,
                                          reconnect_delay_ms * 2);
        }
    }

    void Connect() {
        auto session = CreateSession(config_);
        session->Connect();
        {
            std::lock_guard<std::mutex> guard(session_mutex_);
            session_ = std::move(session);
        }
        LOG_INFO("[relay] worker={} connected to {}:{}{}",
                 config_.name,
                 config_.host,
                 config_.port,
                 config_.path);
        SendJson(BuildActivityPayload("idle", "Connected and idle"));
        StartHeartbeat();
    }

    void ReadLoop() {
        while (running_) {
            const auto text = ReadText();
            if (text.empty()) {
                continue;
            }
            auto json = Json::parse(text, nullptr, false);
            if (json.is_discarded() || !json.is_object()) {
                LOG_WARN("[relay] worker={} ignored invalid message", config_.name);
                continue;
            }
            HandleMessage(json);
        }
    }

    void HandleMessage(const Json& json) {
        const auto type = json.value("type", std::string());
        if (type != "command.dispatch") {
            LOG_DEBUG("[relay] worker={} ignored message type={}", config_.name, type);
            return;
        }

        const auto command_id = json.value("commandId", std::string());
        const auto agent_id = json.value("agentId", std::string());
        const auto payload = json.value("payload", std::string());
        if (command_id.empty()) {
            LOG_WARN("[relay] worker={} received dispatch without commandId", config_.name);
            return;
        }
        if (!agent_id.empty() && agent_id != config_.agent_id) {
            LOG_WARN("[relay] worker={} ignored dispatch for agentId={}", config_.name, agent_id);
            return;
        }

        SendJson(BuildAckPayload(command_id));
        SendJson(BuildActivityPayload("busy", "Executing relay command", command_id));
        SendJson(BuildResultPayload(command_id, "running", std::string(), std::string(), 0));

        try {
            const auto observer = [this, &command_id](kabot::agent::DirectExecutionPhase phase) {
                SendJson(BuildActivityPayload("busy", PhaseActivitySummary(phase), command_id));
            };
            const auto result = agents_.ProcessDirect(config_.local_agent,
                                                      payload,
                                                      BuildSessionKey(config_, command_id),
                                                      observer);
            SendJson(BuildResultPayload(command_id, "completed", "result", result, 100));
            SendJson(BuildActivityPayload("idle", "Idle"));
        } catch (const std::exception& ex) {
            SendJson(BuildResultPayload(command_id, "failed", "error", ex.what()));
            SendJson(BuildActivityPayload("error", ex.what(), command_id));
        } catch (...) {
            SendJson(BuildResultPayload(command_id, "failed", "error", "unknown relay execution error"));
            SendJson(BuildActivityPayload("error", "unknown relay execution error", command_id));
        }
    }

    void StartHeartbeat() {
        StopHeartbeat();
        {
            std::lock_guard<std::mutex> guard(heartbeat_mutex_);
            heartbeat_running_ = true;
        }
        heartbeat_thread_ = std::thread([this] {
            std::unique_lock<std::mutex> lock(heartbeat_mutex_);
            while (heartbeat_running_ && running_) {
                if (heartbeat_cv_.wait_for(
                        lock,
                        std::chrono::seconds(config_.heartbeat_interval_s),
                        [this] { return !heartbeat_running_ || !running_; })) {
                    break;
                }
                lock.unlock();
                try {
                    SendJson(Json{{"type", "heartbeat"}});
                } catch (const std::exception& ex) {
                    LOG_WARN("[relay] worker={} heartbeat failed: {}", config_.name, ex.what());
                    break;
                } catch (...) {
                    LOG_WARN("[relay] worker={} heartbeat failed", config_.name);
                    break;
                }
                lock.lock();
            }
        });
    }

    void StopHeartbeat() {
        {
            std::lock_guard<std::mutex> guard(heartbeat_mutex_);
            heartbeat_running_ = false;
        }
        heartbeat_cv_.notify_all();
        if (heartbeat_thread_.joinable()) {
            heartbeat_thread_.join();
        }
    }

    void SendJson(const Json& payload) {
        SendText(payload.dump());
    }

    void SendText(const std::string& payload) {
        auto session = GetSession();
        if (!session) {
            throw std::runtime_error("relay websocket session is not connected");
        }
        std::lock_guard<std::mutex> guard(write_mutex_);
        session->WriteText(payload);
    }

    std::string ReadText() {
        auto session = GetSession();
        if (!session) {
            throw std::runtime_error("relay websocket session is not connected");
        }
        return session->ReadText();
    }

    std::shared_ptr<IWebSocketSession> GetSession() {
        std::lock_guard<std::mutex> guard(session_mutex_);
        return session_;
    }

    void CloseSession() {
        std::shared_ptr<IWebSocketSession> session;
        {
            std::lock_guard<std::mutex> guard(session_mutex_);
            session = std::move(session_);
        }
        if (session) {
            std::lock_guard<std::mutex> guard(write_mutex_);
            session->Close();
        }
    }

    kabot::config::RelayManagedAgentConfig config_;
    kabot::agent::AgentRegistry& agents_;
    std::atomic<bool> running_{false};
    std::atomic<bool> heartbeat_running_{false};
    std::thread worker_;
    std::thread heartbeat_thread_;
    std::mutex heartbeat_mutex_;
    std::condition_variable heartbeat_cv_;
    std::mutex reconnect_mutex_;
    std::condition_variable reconnect_cv_;
    std::mutex session_mutex_;
    std::mutex write_mutex_;
    std::shared_ptr<IWebSocketSession> session_;
};

RelayManager::RelayManager(const kabot::config::Config& config,
                           kabot::agent::AgentRegistry& agents)
    : config_(config)
    , agents_(agents) {
    for (const auto& relay_agent : config_.relay.managed_agents) {
        workers_.push_back(std::make_unique<Worker>(relay_agent, agents_));
        if (!relay_agent.local_agent.empty()) {
            workers_by_local_agent_[relay_agent.local_agent] = workers_.back().get();
        }
    }
    agents_.SetInboundExecutionReporter(
        [this](const std::string& agent_name,
               kabot::agent::DirectExecutionPhase phase,
               bool success,
               const std::string& summary) {
            auto it = workers_by_local_agent_.find(agent_name);
            if (it == workers_by_local_agent_.end() || it->second == nullptr) {
                return;
            }
            if (summary.empty() && !success) {
                it->second->ReportInboundPhase(phase);
                return;
            }
            it->second->ReportInboundCompletion(success, summary);
        });
}

RelayManager::~RelayManager() {
    Stop();
}

void RelayManager::Start() {
    if (running_) {
        return;
    }
    running_ = true;
    for (auto& worker : workers_) {
        worker->Start();
    }
}

void RelayManager::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    for (auto& worker : workers_) {
        worker->Stop();
    }
}

std::vector<std::string> RelayManager::ManagedLocalAgents() const {
    std::vector<std::string> local_agents;
    local_agents.reserve(config_.relay.managed_agents.size());
    for (const auto& relay_agent : config_.relay.managed_agents) {
        if (!relay_agent.enabled || relay_agent.local_agent.empty()) {
            continue;
        }
        local_agents.push_back(relay_agent.local_agent);
    }
    return local_agents;
}

bool RelayManager::HasManagedLocalAgent(const std::string& local_agent) const {
    return workers_by_local_agent_.find(local_agent) != workers_by_local_agent_.end();
}

RelayTaskClaimResult RelayManager::ClaimNextTask(const std::string& local_agent,
                                                 bool supports_interaction) {
    const auto it = workers_by_local_agent_.find(local_agent);
    if (it == workers_by_local_agent_.end() || it->second == nullptr) {
        return {false, false, 0, "no relay managed agent bound to local agent: " + local_agent, {}};
    }
    return it->second->ClaimNextTask(supports_interaction);
}

RelayTaskStatusUpdateResult RelayManager::UpdateTaskStatus(const std::string& local_agent,
                                                           const std::string& task_id,
                                                           const RelayTaskStatusUpdate& update) {
    const auto it = workers_by_local_agent_.find(local_agent);
    if (it == workers_by_local_agent_.end() || it->second == nullptr) {
        return {false, 0, "no relay managed agent bound to local agent: " + local_agent};
    }
    return it->second->UpdateTaskStatus(task_id, update);
}

DailySummaryUploadResult RelayManager::UploadDailySummary(const std::string& local_agent,
                                                          const std::string& summary_date,
                                                          const std::string& content,
                                                          const std::string& reported_at) {
    const auto it = workers_by_local_agent_.find(local_agent);
    if (it == workers_by_local_agent_.end() || it->second == nullptr) {
        return {false, 0, "no relay managed agent bound to local agent: " + local_agent};
    }
    return it->second->UploadDailySummary(summary_date, content, reported_at);
}

}  // namespace kabot::relay
