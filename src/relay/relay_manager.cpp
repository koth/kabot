#include "relay/relay_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <iomanip>
#include <optional>
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

std::string BuildSessionKey(const kabot::config::RelayManagedAgentConfig& config,
                            const std::string& command_id) {
    return "relay:" + config.name + ":" + command_id;
}

bool IsIpLiteral(const std::string& host) {
    boost::system::error_code ec;
    net::ip::make_address(host, ec);
    return !ec;
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
        auto endpoints = resolver_.resolve(config_.host, port);
        auto endpoint = net::connect(ws_.next_layer(), endpoints);
        auto host = config_.host + ":" + std::to_string(endpoint.port());
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws_.handshake(host, BuildTarget(config_));
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
        auto endpoints = resolver_.resolve(config_.host, port);
        auto endpoint = beast::get_lowest_layer(ws_).connect(endpoints);
        auto host = config_.host + ":" + std::to_string(endpoint.port());
        if (!IsIpLiteral(config_.host)) {
            if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), config_.host.c_str())) {
                throw std::runtime_error("failed to set TLS SNI host");
            }
        }
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws_.next_layer().handshake(ssl::stream_base::client);
        ws_.handshake(host, BuildTarget(config_));
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

private:
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
            const auto result = agents_.ProcessDirect(config_.local_agent,
                                                      payload,
                                                      BuildSessionKey(config_, command_id));
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
    }
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

}  // namespace kabot::relay
