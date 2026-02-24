#include "channels/lark_channel.hpp"

#include <iostream>

#include <nlohmann/json.hpp>

#include "lark/im/v1/im_service.h"
#include "lark/im/v1/model.h"
#include "lark/ws/event_dispatcher.h"

// lark_cpp 的 WsClient::Stop 是私有接口，这里通过宏打开访问以便安全停止线程。
#define private public
#include "lark/ws/ws_client.h"
#undef private

namespace kabot::channels {

LarkChannel::LarkChannel(const kabot::config::LarkConfig& config,
                         kabot::bus::MessageBus& bus)
    : ChannelBase("lark", bus, config.allow_from)
    , config_(config) {
    lark_config_.app_id = config_.app_id;
    lark_config_.app_secret = config_.app_secret;
    lark_config_.domain = config_.domain.empty() ? "https://open.feishu.cn" : config_.domain;
    lark_config_.timeout_ms = config_.timeout_ms;
}

void LarkChannel::Start() {
    if (running_) {
        return;
    }
    if (config_.app_id.empty() || config_.app_secret.empty()) {
        std::cerr << "[lark] app_id or app_secret is empty; channel disabled" << std::endl;
        running_ = false;
        return;
    }

    lark_config_.app_id = config_.app_id;
    lark_config_.app_secret = config_.app_secret;
    lark_config_.domain = config_.domain.empty() ? "https://open.feishu.cn" : config_.domain;
    lark_config_.timeout_ms = config_.timeout_ms;

    dispatcher_ = std::make_unique<lark::ws::EventDispatcher>();
    dispatcher_->OnMessageReceive([this](const lark::im::v1::MessageEvent& event) {
        HandleIncomingMessage(event);
    });

    ws_client_ = std::make_unique<lark::ws::WsClient>(lark_config_, *dispatcher_);
    im_service_ = std::make_unique<lark::im::v1::ImService>(lark_config_);

    running_ = true;
    ws_thread_ = std::make_unique<std::thread>([this]() {
        if (ws_client_) {
            ws_client_->Start();
        }
    });
}

void LarkChannel::Stop() {
    running_ = false;
    if (ws_client_) {
        ws_client_->Stop();
    }
    if (ws_thread_ && ws_thread_->joinable()) {
        ws_thread_->join();
    }
    ws_thread_.reset();
    ws_client_.reset();
    dispatcher_.reset();
    im_service_.reset();
}

void LarkChannel::Send(const kabot::bus::OutboundMessage& msg) {
    if (!im_service_) {
        return;
    }
    auto it_action = msg.metadata.find("action");
    if (it_action != msg.metadata.end() && it_action->second == "typing") {
        return;
    }

    std::string chat_id = msg.chat_id;
    if (chat_id.empty() && !msg.reply_to.empty()) {
        auto it = message_chat_ids_.find(msg.reply_to);
        if (it != message_chat_ids_.end()) {
            chat_id = it->second;
        }
    }
    if (chat_id.empty()) {
        return;
    }

    const std::string msg_type = "text";
    nlohmann::json body;
    body["text"] = msg.content;

    if (!im_service_->CreateMessage(chat_id, msg_type, body.dump())) {
        std::cerr << "[lark] failed to send message" << std::endl;
    }
}

void LarkChannel::HandleIncomingMessage(const lark::im::v1::MessageEvent& event) {
    if (event.chat_id.empty()) {
        return;
    }
    const auto content = ExtractTextFromContent(event.msg_type, event.content);

    std::unordered_map<std::string, std::string> metadata;
    metadata["message_id"] = event.message_id;
    metadata["sender_id"] = event.sender_id;
    metadata["msg_type"] = event.msg_type;
    metadata["raw_content"] = event.content;
    metadata["chat_id"] = event.chat_id;

    if (!event.message_id.empty()) {
        message_chat_ids_[event.message_id] = event.chat_id;
    }

    HandleMessage(event.sender_id, event.chat_id, content, {}, metadata);
}

std::string LarkChannel::ExtractTextFromContent(const std::string& msg_type,
                                                const std::string& content) const {
    if (msg_type == "text") {
        auto json = nlohmann::json::parse(content, nullptr, false);
        if (!json.is_discarded() && json.contains("text") && json["text"].is_string()) {
            return json["text"].get<std::string>();
        }
    }
    return content;
}

}  // namespace kabot::channels
