#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "channels/channel_base.hpp"
#include "config/config_schema.hpp"
#include "lark/core/config.h"

namespace kabot::channels {

namespace lark::im::v1 {
struct MessageEvent;
}  // namespace lark::im::v1

namespace lark::ws {
class EventDispatcher;
class WsClient;
}  // namespace lark::ws

namespace lark::im::v1 {
class ImService;
}  // namespace lark::im::v1

class LarkChannel : public ChannelBase {
public:
    LarkChannel(const kabot::config::LarkConfig& config,
                kabot::bus::MessageBus& bus);

    void Start() override;
    void Stop() override;
    void Send(const kabot::bus::OutboundMessage& msg) override;

private:
    void HandleIncomingMessage(const lark::im::v1::MessageEvent& event);
    std::string ExtractTextFromContent(const std::string& msg_type,
                                       const std::string& content) const;

    kabot::config::LarkConfig config_;
    lark::core::Config lark_config_;

    std::unique_ptr<lark::ws::EventDispatcher> dispatcher_;
    std::unique_ptr<lark::ws::WsClient> ws_client_;
    std::unique_ptr<lark::im::v1::ImService> im_service_;
    std::unique_ptr<std::thread> ws_thread_;
    std::atomic<bool> running_{false};

    std::unordered_map<std::string, std::string> message_chat_ids_;
};

}  // namespace kabot::channels
