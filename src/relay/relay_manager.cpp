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

std::string BuildProjectTaskSubmissionTarget(const std::string& project_id) {
    return "/api/projects/" + UrlEncode(project_id) + "/tasks";
}

std::string BuildProjectQueryTarget(const std::string& project_id) {
    return "/api/projects/" + UrlEncode(project_id);
}

http::request<http::string_body> BuildProjectQueryRequest(
    const kabot::config::RelayManagedAgentConfig& config,
    const std::string& project_id) {
    http::request<http::string_body> request{http::verb::get, BuildProjectQueryTarget(project_id), 11};
    request.set(http::field::host, config.host);
    request.set(http::field::user_agent, "kabot-relay/1.0");
    request.set(http::field::authorization, "Bearer " + config.token);
    request.prepare_payload();
    return request;
}

http::request<http::string_body> BuildSubmitTaskRequest(
    const kabot::config::RelayManagedAgentConfig& config,
    const std::string& project_id,
    const kabot::relay::RelayTaskCreate& task) {
    Json body = {
        {"title", task.title},
        {"instruction", task.instruction}
    };
    if (!task.priority.empty()) {
        body["priority"] = task.priority;
    }
    if (!task.project.project_id.empty()) {
        body["projectId"] = task.project.project_id;
    }
    if (!task.merge_request.empty()) {
        body["mergeRequest"] = task.merge_request;
    }
    if (!task.interaction.channel.empty()) {
        body["interaction"] = {
            {"channel", task.interaction.channel},
            {"channelInstance", task.interaction.channel_instance},
            {"chatId", task.interaction.chat_id},
            {"replyTo", task.interaction.reply_to}
        };
    }
    if (!task.depends_on.empty()) {
        body["dependsOn"] = task.depends_on;
    }
    if (!task.metadata.empty()) {
        body["metadata"] = task.metadata;
    }

    http::request<http::string_body> request{http::verb::post, BuildProjectTaskSubmissionTarget(project_id), 11};
    request.set(http::field::host, config.host);
    request.set(http::field::user_agent, "kabot-relay/1.0");
    request.set(http::field::authorization, "Bearer " + config.token);
    request.set(http::field::content_type, "application/json");
    request.body() = body.dump();
    request.prepare_payload();
    return request;
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

std::vector<std::string> ParseStringArray(const Json& json, const std::string& key) {
    std::vector<std::string> result;
    if (!json.contains(key) || !json[key].is_array()) {
        return result;
    }
    for (const auto& item : json[key].items()) {
        if (item.value().is_string()) {
            result.push_back(item.value().get<std::string>());
        }
    }
    return result;
}

std::unordered_map<std::string, std::string> ParseStringMap(const Json& json, const std::string& key) {
    std::unordered_map<std::string, std::string> result;
    if (!json.contains(key) || !json[key].is_object()) {
        return result;
    }
    for (const auto& [k, v] : json[key].items()) {
        if (v.is_string()) {
            result[k] = v.get<std::string>();
        } else if (!v.is_null()) {
            result[k] = v.dump();
        }
    }
    return result;
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
    task.waiting_user = json.value("waitingUser", false);
    task.merge_request = json.value("mergeRequest", std::string());
    
    if (json.contains("interaction") && json["interaction"].is_object()) {
        const auto& interaction = json["interaction"];
        task.interaction.channel = interaction.value("channel", std::string());
        task.interaction.channel_instance = interaction.value("channelInstance", std::string());
        task.interaction.chat_id = interaction.value("chatId", std::string());
        task.interaction.reply_to = interaction.value("replyTo", std::string());
    }
    
    if (json.contains("metadata") && json["metadata"].is_object()) {
        task.metadata = ParseStringMap(json, "metadata");
    }
    
    // Parse project context
    if (json.contains("project") && json["project"].is_object()) {
        const auto& project = json["project"];
        task.project.project_id = project.value("projectId", std::string());
        task.project.name = project.value("name", std::string());
        task.project.description = project.value("description", std::string());
        task.project.git_url = project.value("gitUrl", std::string());
        if (project.contains("metadata") && project["metadata"].is_object()) {
            task.project.metadata = ParseStringMap(project, "metadata");
        }
    }
    
    // Parse dependency context
    task.depends_on_task_ids = ParseStringArray(json, "dependsOnTaskIds");
    task.blocked_by_task_ids = ParseStringArray(json, "blockedByTaskIds");
    task.dependency_state = ParseStringMap(json, "dependencyState");
    
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

class IRelayHttpSession {
public:
    virtual ~IRelayHttpSession() = default;
    virtual http::response<http::string_body> DoRequest(http::request<http::string_body> request) = 0;
    virtual void Close() = 0;
};

class PlainRelayHttpSession final : public IRelayHttpSession {
public:
    explicit PlainRelayHttpSession(const kabot::config::RelayManagedAgentConfig& config)
        : config_(config)
        , resolver_(ioc_) {}

    http::response<http::string_body> DoRequest(http::request<http::string_body> request) override {
        request.set(http::field::connection, "keep-alive");
        if (!connected_) {
            Connect();
        }
        try {
            beast::flat_buffer buffer;
            http::response<http::string_body> response;
            http::write(stream_, request);
            http::read(stream_, buffer, response);
            return response;
        } catch (const std::exception&) {
            connected_ = false;
            Close();
            Connect();
            beast::flat_buffer buffer;
            http::response<http::string_body> response;
            http::write(stream_, request);
            http::read(stream_, buffer, response);
            return response;
        }
    }

    void Close() override {
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
        stream_.socket().close(ec);
        connected_ = false;
    }

private:
    void Connect() {
        auto endpoints = resolver_.resolve(config_.host, std::to_string(config_.port));
        stream_.connect(endpoints);
        connected_ = true;
    }

    kabot::config::RelayManagedAgentConfig config_;
    net::io_context ioc_;
    tcp::resolver resolver_;
    beast::tcp_stream stream_{ioc_};
    bool connected_ = false;
};

class TlsRelayHttpSession final : public IRelayHttpSession {
public:
    explicit TlsRelayHttpSession(const kabot::config::RelayManagedAgentConfig& config)
        : config_(config)
        , resolver_(ioc_)
        , ssl_ctx_(ssl::context::tls_client) {
        ssl_ctx_.set_default_verify_paths();
        ssl_ctx_.set_verify_mode(config_.skip_tls_verify ? ssl::verify_none : ssl::verify_peer);
    }

    http::response<http::string_body> DoRequest(http::request<http::string_body> request) override {
        request.set(http::field::connection, "keep-alive");
        if (!connected_) {
            Connect();
        }
        try {
            beast::flat_buffer buffer;
            http::response<http::string_body> response;
            http::write(*stream_, request);
            http::read(*stream_, buffer, response);
            return response;
        } catch (const std::exception&) {
            connected_ = false;
            Close();
            Connect();
            beast::flat_buffer buffer;
            http::response<http::string_body> response;
            http::write(*stream_, request);
            http::read(*stream_, buffer, response);
            return response;
        }
    }

    void Close() override {
        if (stream_) {
            beast::error_code ec;
            beast::get_lowest_layer(*stream_).socket().shutdown(tcp::socket::shutdown_both, ec);
            beast::get_lowest_layer(*stream_).socket().close(ec);
            stream_.reset();
        }
        connected_ = false;
    }

private:
    void Connect() {
        stream_ = std::make_unique<beast::ssl_stream<beast::tcp_stream>>(ioc_, ssl_ctx_);
        if (!IsIpLiteral(config_.host)) {
            if (!SSL_set_tlsext_host_name(stream_->native_handle(), config_.host.c_str())) {
                throw std::runtime_error("failed to set TLS SNI host");
            }
        }
        auto endpoints = resolver_.resolve(config_.host, std::to_string(config_.port));
        beast::get_lowest_layer(*stream_).connect(endpoints);
        stream_->handshake(ssl::stream_base::client);
        connected_ = true;
    }

    kabot::config::RelayManagedAgentConfig config_;
    net::io_context ioc_;
    tcp::resolver resolver_;
    ssl::context ssl_ctx_;
    std::unique_ptr<beast::ssl_stream<beast::tcp_stream>> stream_;
    bool connected_ = false;
};

std::unique_ptr<IRelayHttpSession> CreateHttpSession(const kabot::config::RelayManagedAgentConfig& config) {
    if (config.use_tls || config.scheme == "wss") {
        return std::make_unique<TlsRelayHttpSession>(config);
    }
    return std::make_unique<PlainRelayHttpSession>(config);
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

http::request<http::string_body> BuildDailySummaryRequest(
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
    return request;
}

http::request<http::string_body> BuildClaimNextTaskRequest(
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
    return request;
}

http::request<http::string_body> BuildTaskStatusRequest(
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
    if (update.merge_request.has_value()) {
        body["mergeRequest"] = {
            {"url", update.merge_request->url},
            {"createdAt", update.merge_request->created_at},
            {"mergedAt", update.merge_request->merged_at.empty() 
                ? nlohmann::json(nullptr) 
                : nlohmann::json(update.merge_request->merged_at)}
        };
    }

    http::request<http::string_body> request{http::verb::post, BuildTaskStatusTarget(config, task_id), 11};
    request.set(http::field::host, config.host);
    request.set(http::field::user_agent, "kabot-relay/1.0");
    request.set(http::field::authorization, "Bearer " + config.token);
    request.set(http::field::content_type, "application/json");
    request.body() = body.dump();
    request.prepare_payload();
    return request;
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

    const kabot::config::RelayManagedAgentConfig& Config() const {
        return config_;
    }

    void Start() {
        if (running_ || !config_.enabled) {
            return;
        }
        running_ = true;
        StartOutboundWriter();
        worker_ = std::thread([this] { Run(); });
    }

    void Stop() {
        running_ = false;
        StopHeartbeat();
        StopOutboundWriter();
        reconnect_cv_.notify_all();
        CloseSession();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    http::response<http::string_body> DoHttpRequest(http::request<http::string_body> request) const {
        return HttpSession()->DoRequest(std::move(request));
    }

    void ReportInboundPhase(kabot::agent::DirectExecutionPhase phase) {
        try {
            EnqueueJson(BuildActivityPayload("busy", kabot::agent::DirectExecutionPhaseSummary(phase)));
        } catch (const std::exception& ex) {
            LOG_WARN("[relay] worker={} inbound phase report failed: {}", config_.name, ex.what());
        } catch (...) {
            LOG_WARN("[relay] worker={} inbound phase report failed", config_.name);
        }
    }

    void ReportInboundCompletion(bool success, const std::string& summary) {
        try {
            if (success) {
                EnqueueJson(BuildActivityPayload("idle", "Idle"));
            } else {
                EnqueueJson(BuildActivityPayload("error",
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
            const auto request = BuildDailySummaryRequest(
                config_, trimmed_date, trimmed_content,
                trimmed_reported_at.empty() ? IsoNow() : trimmed_reported_at);
            const auto response = HttpSession()->DoRequest(request);
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
            const auto request = BuildClaimNextTaskRequest(config_, supports_interaction);
            const auto response = HttpSession()->DoRequest(request);
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
            const auto request = BuildTaskStatusRequest(config_, trimmed_task_id, update);
            const auto response = HttpSession()->DoRequest(request);
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
    IRelayHttpSession* HttpSession() const {
        if (!http_session_) {
            http_session_ = CreateHttpSession(config_);
        }
        return http_session_.get();
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
            kabot::CancelToken cancel_token{};
            const auto result = agents_.ProcessDirect(config_.local_agent,
                                                      payload,
                                                      BuildSessionKey(config_, command_id),
                                                      observer,
                                                      {},
                                                      {},
                                                      cancel_token);
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

    void StartOutboundWriter() {
        StopOutboundWriter();
        {
            std::lock_guard<std::mutex> guard(outbound_writer_mutex_);
            outbound_writer_running_ = true;
        }
        outbound_writer_thread_ = std::thread([this] {
            std::unique_lock<std::mutex> lock(outbound_writer_mutex_);
            while (outbound_writer_running_) {
                outbound_writer_cv_.wait(lock, [this] {
                    return !outbound_writer_running_ || !outbound_queue_.empty();
                });
                if (!outbound_writer_running_) {
                    break;
                }
                std::queue<Json> local;
                local.swap(outbound_queue_);
                lock.unlock();
                while (!local.empty()) {
                    try {
                        SendJson(local.front());
                    } catch (const std::exception& ex) {
                        LOG_WARN("[relay] worker={} outbound write failed: {}", config_.name, ex.what());
                    } catch (...) {
                        LOG_WARN("[relay] worker={} outbound write failed", config_.name);
                    }
                    local.pop();
                }
                lock.lock();
            }
        });
    }

    void StopOutboundWriter() {
        {
            std::lock_guard<std::mutex> guard(outbound_writer_mutex_);
            outbound_writer_running_ = false;
        }
        outbound_writer_cv_.notify_all();
        if (outbound_writer_thread_.joinable()) {
            outbound_writer_thread_.join();
        }
    }

    void EnqueueJson(const Json& payload) {
        {
            std::lock_guard<std::mutex> guard(outbound_writer_mutex_);
            if (!outbound_writer_running_) {
                return;
            }
            outbound_queue_.push(payload);
        }
        outbound_writer_cv_.notify_one();
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
    mutable std::unique_ptr<IRelayHttpSession> http_session_;
    std::queue<Json> outbound_queue_;
    std::mutex outbound_writer_mutex_;
    std::condition_variable outbound_writer_cv_;
    std::thread outbound_writer_thread_;
    bool outbound_writer_running_ = false;
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

std::vector<std::string> RelayManager::AutoClaimLocalAgents() const {
    std::vector<std::string> local_agents;
    local_agents.reserve(config_.relay.managed_agents.size());
    for (const auto& relay_agent : config_.relay.managed_agents) {
        if (!relay_agent.enabled || relay_agent.local_agent.empty() || !relay_agent.auto_claim_tasks) {
            continue;
        }
        local_agents.push_back(relay_agent.local_agent);
    }
    return local_agents;
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

RelayTaskSubmissionResult RelayManager::SubmitProjectTask(const std::string& project_id,
                                                           const RelayTaskCreate& task) {
    if (workers_.empty()) {
        return {false, 0, "no relay workers available", {}};
    }
    if (project_id.empty()) {
        return {false, 0, "project_id is required for task submission", {}};
    }
    try {
        const auto& worker = workers_.front();
        const auto request = BuildSubmitTaskRequest(worker->Config(), project_id, task);
        const auto response = worker->DoHttpRequest(request);
        const bool success = response.result_int() >= 200 && response.result_int() < 300;
        std::string message;
        std::string returned_task_id;
        if (!success) {
            message = !response.body().empty()
                ? response.body()
                : std::string(response.reason());
        } else {
            try {
                auto json = Json::parse(response.body(), nullptr, false);
                if (json.is_object() && json.contains("taskId") && json["taskId"].is_string()) {
                    returned_task_id = json["taskId"].get<std::string>();
                }
            } catch (...) {
                // ignore parse failure; we still succeeded at HTTP level
            }
        }
        return {success, static_cast<int>(response.result_int()), message, returned_task_id};
    } catch (const std::exception& ex) {
        return {false, 0, ex.what(), {}};
    } catch (...) {
        return {false, 0, "unknown task submission error", {}};
    }
}

RelayProjectQueryResult RelayManager::QueryProject(const std::string& project_id) {
    if (workers_.empty()) {
        return {false, 0, "no relay workers available", {}};
    }
    if (project_id.empty()) {
        return {false, 0, "project_id is required for project query", {}};
    }
    try {
        const auto& worker = workers_.front();
        const auto request = BuildProjectQueryRequest(worker->Config(), project_id);
        const auto response = worker->DoHttpRequest(request);
        const bool success = response.result_int() >= 200 && response.result_int() < 300;
        if (!success) {
            std::string message = !response.body().empty()
                ? response.body()
                : std::string(response.reason());
            return {false, static_cast<int>(response.result_int()), message, {}};
        }
        auto json = Json::parse(response.body(), nullptr, false);
        if (!json.is_object()) {
            return {false, static_cast<int>(response.result_int()), "invalid JSON response", {}};
        }
        RelayProjectInfo info;
        info.project_id = project_id;
        if (json.contains("projectId") && json["projectId"].is_string()) {
            info.project_id = json["projectId"].get<std::string>();
        }
        if (json.contains("name") && json["name"].is_string()) {
            info.name = json["name"].get<std::string>();
        }
        if (json.contains("description") && json["description"].is_string()) {
            info.description = json["description"].get<std::string>();
        }
        if (json.contains("metadata") && json["metadata"].is_object()) {
            for (const auto& [k, v] : json["metadata"].items()) {
                if (v.is_string()) {
                    info.metadata[k] = v.get<std::string>();
                }
            }
        }
        return {true, static_cast<int>(response.result_int()), {}, info};
    } catch (const std::exception& ex) {
        return {false, 0, ex.what(), {}};
    } catch (...) {
        return {false, 0, "unknown project query error", {}};
    }
}

}  // namespace kabot::relay
