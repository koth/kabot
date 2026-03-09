#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "nlohmann/json.hpp"
#include "channels/channel_base.hpp"
#include "config/config_schema.hpp"

namespace qqbot::openapi::v1 {
class OpenAPIV1Client;
}

namespace qqbot::websocket {
class WebSocketClient;
}

namespace kabot::channels {

class QQBotChannel : public ChannelBase {
public:
    QQBotChannel(const kabot::config::QQBotConfig& config,
                 kabot::bus::MessageBus& bus);

    void Start() override;
    void Stop() override;
    bool Send(const kabot::bus::OutboundMessage& msg) override;

private:
    struct TargetContext {
        std::string type;
        std::string chat_id;
        std::string channel_id;
        std::string guild_id;
        std::string group_openid;
        std::string user_openid;
        std::string message_id;
    };

    void HandleGatewayEvent(const std::string& event_name, const std::string& payload);
    void HandleChannelEvent(const std::string& event_name, const std::string& payload);
    void HandleDirectEvent(const std::string& event_name, const std::string& payload);
    void HandleC2CEvent(const std::string& event_name, const std::string& payload);
    void HandleGroupEvent(const std::string& event_name, const std::string& payload);

    void PublishMessage(const std::string& sender_id,
                        const std::string& chat_id,
                        const std::string& content,
                        std::unordered_map<std::string, std::string> metadata,
                        TargetContext target);

    TargetContext ResolveTarget(const kabot::bus::OutboundMessage& msg) const;
    void RememberTarget(const std::string& chat_id,
                        const std::string& message_id,
                        const TargetContext& target);

    static std::string JsonString(const nlohmann::json& value,
                                  std::initializer_list<const char*> keys);
    static bool JsonBool(const nlohmann::json& value,
                         std::initializer_list<const char*> keys,
                         bool fallback = false);

    kabot::config::QQBotConfig config_;
    std::shared_ptr<qqbot::openapi::v1::OpenAPIV1Client> openapi_v1_;
    std::shared_ptr<qqbot::websocket::WebSocketClient> websocket_;
    std::atomic<bool> running_{false};
    mutable std::mutex target_mutex_;
    std::unordered_map<std::string, TargetContext> message_targets_;
    std::unordered_map<std::string, TargetContext> chat_targets_;
};

}  // namespace kabot::channels
