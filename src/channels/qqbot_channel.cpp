#include "channels/qqbot_channel.hpp"

#include <utility>

#include "nlohmann/json.hpp"
#include "openapi/v1/openapi_v1.h"
#include "sdk/bot_sdk.h"
#include "utils/logging.hpp"
#include "websocket/websocket_client.h"

namespace kabot::channels {

namespace {

using Json = nlohmann::json;

qqbot::common::BotConfig ToBotConfig(const kabot::config::QQBotConfig& config) {
    qqbot::common::BotConfig bot_config;
    bot_config.app_id = config.app_id;
    bot_config.client_secret = config.client_secret;
    bot_config.token = config.token;
    bot_config.sandbox = config.sandbox;
    bot_config.skip_tls_verify = config.skip_tls_verify;
    if (!config.intents.empty()) {
        try {
            bot_config.intents = std::stoi(config.intents);
        } catch (...) {
            bot_config.intents = qqbot::common::intents::kMessageReplyExample;
        }
    } else {
        bot_config.intents = qqbot::common::intents::kMessageReplyExample;
    }
    return bot_config;
}

bool ResponseOk(const qqbot::transport::HttpResponse& response) {
    return response.status_code >= 200 && response.status_code < 300;
}

}  // namespace

QQBotChannel::QQBotChannel(const kabot::config::QQBotConfig& config,
                           kabot::bus::MessageBus& bus)
    : ChannelBase(config.name.empty() ? "qqbot" : config.name,
                  bus,
                  config.allow_from,
                  config.binding.agent)
    , config_(config) {}

std::string QQBotChannel::JsonString(const Json& value,
                                     std::initializer_list<const char*> keys) {
    for (const auto* key : keys) {
        if (!value.contains(key)) {
            continue;
        }
        if (value[key].is_string()) {
            return value[key].get<std::string>();
        }
    }
    return {};
}

bool QQBotChannel::JsonBool(const Json& value,
                            std::initializer_list<const char*> keys,
                            bool fallback) {
    for (const auto* key : keys) {
        if (!value.contains(key)) {
            continue;
        }
        if (value[key].is_boolean()) {
            return value[key].get<bool>();
        }
    }
    return fallback;
}

void QQBotChannel::Start() {
    if (running_) {
        return;
    }
    if (config_.app_id.empty() || (config_.client_secret.empty() && config_.token.empty())) {
        LOG_ERROR("[qqbot] app_id or credentials are empty; channel disabled");
        running_ = false;
        return;
    }

    try {
        auto openapi = qqbot::sdk::CreateOpenAPI(ToBotConfig(config_));
        openapi_v1_ = std::dynamic_pointer_cast<qqbot::openapi::v1::OpenAPIV1Client>(openapi);
        websocket_ = qqbot::sdk::CreateWebSocket(ToBotConfig(config_));
        if (!openapi_v1_ || !websocket_) {
            LOG_ERROR("[qqbot] failed to initialize openapi or websocket client");
            running_ = false;
            return;
        }
        websocket_->SetEventHandler([this](const qqbot::websocket::GatewayEvent& event) {
            if (event.type != qqbot::websocket::EventType::kDispatch) {
                return;
            }
            try {
                HandleGatewayEvent(event.event_name, event.payload);
            } catch (const std::exception& ex) {
                LOG_ERROR("[qqbot] failed to handle event {}: {}", event.event_name, ex.what());
            } catch (...) {
                LOG_ERROR("[qqbot] failed to handle event {}", event.event_name);
            }
        });
        websocket_->Connect();
        running_ = true;
    } catch (const std::exception& ex) {
        LOG_ERROR("[qqbot] failed to start: {}", ex.what());
        websocket_.reset();
        openapi_v1_.reset();
        running_ = false;
    }
}

void QQBotChannel::Stop() {
    running_ = false;
    if (websocket_) {
        try {
            websocket_->Disconnect();
        } catch (...) {
        }
    }
    websocket_.reset();
    openapi_v1_.reset();
}

void QQBotChannel::RememberTarget(const std::string& chat_id,
                                  const std::string& message_id,
                                  const TargetContext& target) {
    std::lock_guard<std::mutex> guard(target_mutex_);
    if (!chat_id.empty()) {
        chat_targets_[chat_id] = target;
    }
    if (!message_id.empty()) {
        message_targets_[message_id] = target;
    }
}

QQBotChannel::TargetContext QQBotChannel::ResolveTarget(const kabot::bus::OutboundMessage& msg) const {
    TargetContext target;
    auto it_type = msg.metadata.find("qqbot_chat_type");
    if (it_type != msg.metadata.end()) {
        target.type = it_type->second;
    }
    auto it_channel = msg.metadata.find("qqbot_channel_id");
    if (it_channel != msg.metadata.end()) {
        target.channel_id = it_channel->second;
    }
    auto it_guild = msg.metadata.find("qqbot_guild_id");
    if (it_guild != msg.metadata.end()) {
        target.guild_id = it_guild->second;
    }
    auto it_group = msg.metadata.find("qqbot_group_openid");
    if (it_group != msg.metadata.end()) {
        target.group_openid = it_group->second;
    }
    auto it_user = msg.metadata.find("qqbot_user_openid");
    if (it_user != msg.metadata.end()) {
        target.user_openid = it_user->second;
    }
    auto it_msg = msg.metadata.find("qqbot_message_id");
    if (it_msg != msg.metadata.end()) {
        target.message_id = it_msg->second;
    }
    target.chat_id = msg.chat_id;

    std::lock_guard<std::mutex> guard(target_mutex_);
    if (!msg.reply_to.empty()) {
        auto it = message_targets_.find(msg.reply_to);
        if (it != message_targets_.end()) {
            return it->second;
        }
    }
    if (!target.type.empty()) {
        return target;
    }
    if (!msg.chat_id.empty()) {
        auto it = chat_targets_.find(msg.chat_id);
        if (it != chat_targets_.end()) {
            return it->second;
        }
    }
    return target;
}

bool QQBotChannel::Send(const kabot::bus::OutboundMessage& msg) {
    if (!openapi_v1_) {
        return false;
    }
    auto it_action = msg.metadata.find("action");
    if (it_action != msg.metadata.end() && it_action->second == "typing") {
        return true;
    }

    const auto target = ResolveTarget(msg);
    if (target.type.empty()) {
        LOG_ERROR("[qqbot] failed to resolve outbound target for chat_id={} reply_to={}", msg.chat_id, msg.reply_to);
        return false;
    }

    Json body;
    body["content"] = msg.content;
    if (target.type == "c2c" || target.type == "group") {
        body["msg_type"] = 0;
        if (!target.message_id.empty()) {
            body["msg_id"] = target.message_id;
        }
    }

    qqbot::transport::HttpResponse response;
    try {
        if (target.type == "channel" && !target.channel_id.empty()) {
            response = openapi_v1_->PostMessage(target.channel_id, body);
        } else if (target.type == "direct" && !target.guild_id.empty()) {
            response = openapi_v1_->PostDirectMessage(target.guild_id, body);
        } else if (target.type == "c2c" && !target.user_openid.empty()) {
            response = openapi_v1_->PostC2CMessage(target.user_openid, body);
        } else if (target.type == "group" && !target.group_openid.empty()) {
            response = openapi_v1_->PostGroupMessage(target.group_openid, body);
        } else {
            LOG_ERROR("[qqbot] outbound target missing identifier type={}", target.type);
            return false;
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("[qqbot] failed to send outbound message: {}", ex.what());
        return false;
    }

    if (!ResponseOk(response)) {
        LOG_ERROR("[qqbot] outbound send failed type={} status={} body={}",
                  target.type,
                  response.status_code,
                  response.body);
        return false;
    }
    return true;
}

void QQBotChannel::PublishMessage(const std::string& sender_id,
                                  const std::string& chat_id,
                                  const std::string& content,
                                  std::unordered_map<std::string, std::string> metadata,
                                  TargetContext target) {
    if (!target.type.empty()) {
        metadata["qqbot_chat_type"] = target.type;
    }
    if (!target.channel_id.empty()) {
        metadata["qqbot_channel_id"] = target.channel_id;
    }
    if (!target.guild_id.empty()) {
        metadata["qqbot_guild_id"] = target.guild_id;
    }
    if (!target.group_openid.empty()) {
        metadata["qqbot_group_openid"] = target.group_openid;
    }
    if (!target.user_openid.empty()) {
        metadata["qqbot_user_openid"] = target.user_openid;
    }
    if (!target.message_id.empty()) {
        metadata["qqbot_message_id"] = target.message_id;
    }
    RememberTarget(chat_id, target.message_id, target);
    HandleMessage(sender_id, chat_id, content, {}, metadata);
}

void QQBotChannel::HandleChannelEvent(const std::string& event_name, const std::string& payload) {
    const auto json = Json::parse(payload, nullptr, false);
    if (!json.is_object()) {
        return;
    }
    const auto author = json.contains("author") && json["author"].is_object() ? json["author"] : Json::object();
    if (JsonBool(author, {"bot"}, false)) {
        return;
    }
    TargetContext target;
    target.type = "channel";
    target.channel_id = JsonString(json, {"channel_id"});
    target.guild_id = JsonString(json, {"guild_id"});
    target.message_id = JsonString(json, {"id", "message_id"});
    target.chat_id = target.channel_id;

    std::unordered_map<std::string, std::string> metadata;
    metadata["event_name"] = event_name;
    const auto sender_id = JsonString(author, {"id", "user_openid", "username"});
    PublishMessage(sender_id, target.chat_id, JsonString(json, {"content"}), std::move(metadata), std::move(target));
}

void QQBotChannel::HandleDirectEvent(const std::string& event_name, const std::string& payload) {
    const auto json = Json::parse(payload, nullptr, false);
    if (!json.is_object()) {
        return;
    }
    TargetContext target;
    target.type = "direct";
    target.channel_id = JsonString(json, {"channel_id"});
    target.guild_id = JsonString(json, {"guild_id"});
    target.message_id = JsonString(json, {"id", "message_id"});
    target.chat_id = !target.guild_id.empty() ? target.guild_id : target.channel_id;

    const auto author = json.contains("author") && json["author"].is_object() ? json["author"] : Json::object();
    std::unordered_map<std::string, std::string> metadata;
    metadata["event_name"] = event_name;
    const auto sender_id = JsonString(author, {"id", "user_openid", "username"});
    PublishMessage(sender_id, target.chat_id, JsonString(json, {"content"}), std::move(metadata), std::move(target));
}

void QQBotChannel::HandleC2CEvent(const std::string& event_name, const std::string& payload) {
    const auto json = Json::parse(payload, nullptr, false);
    if (!json.is_object()) {
        return;
    }
    const auto author = json.contains("author") && json["author"].is_object() ? json["author"] : Json::object();
    TargetContext target;
    target.type = "c2c";
    target.user_openid = JsonString(author, {"user_openid", "id"});
    target.message_id = JsonString(json, {"id", "message_id"});
    target.chat_id = target.user_openid;

    std::unordered_map<std::string, std::string> metadata;
    metadata["event_name"] = event_name;
    PublishMessage(target.user_openid, target.chat_id, JsonString(json, {"content"}), std::move(metadata), std::move(target));
}

void QQBotChannel::HandleGroupEvent(const std::string& event_name, const std::string& payload) {
    const auto json = Json::parse(payload, nullptr, false);
    if (!json.is_object()) {
        return;
    }
    const auto author = json.contains("author") && json["author"].is_object() ? json["author"] : Json::object();
    TargetContext target;
    target.type = "group";
    target.group_openid = JsonString(json, {"group_openid"});
    target.message_id = JsonString(json, {"id", "message_id"});
    target.chat_id = target.group_openid;

    std::unordered_map<std::string, std::string> metadata;
    metadata["event_name"] = event_name;
    const auto sender_id = JsonString(author, {"member_openid", "user_openid", "id"});
    PublishMessage(sender_id, target.chat_id, JsonString(json, {"content"}), std::move(metadata), std::move(target));
}

void QQBotChannel::HandleGatewayEvent(const std::string& event_name, const std::string& payload) {
    if (event_name == "DIRECT_MESSAGE_CREATE") {
        HandleDirectEvent(event_name, payload);
        return;
    }
    if (event_name == "C2C_MESSAGE_CREATE") {
        HandleC2CEvent(event_name, payload);
        return;
    }
    if (event_name == "GROUP_AT_MESSAGE_CREATE" || event_name == "GROUP_MSG_RECEIVE") {
        HandleGroupEvent(event_name, payload);
        return;
    }
    if (event_name == "MESSAGE_CREATE" || event_name == "AT_MESSAGE_CREATE") {
        HandleChannelEvent(event_name, payload);
    }
}

}  // namespace kabot::channels
