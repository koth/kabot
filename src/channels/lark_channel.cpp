#include "channels/lark_channel.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
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
namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string GetExtensionLower(const std::string& path) {
    std::filesystem::path fs_path(path);
    return ToLower(fs_path.extension().string());
}

std::string GetFileName(const std::string& path) {
    std::filesystem::path fs_path(path);
    return fs_path.filename().string();
}

bool IsImageExtension(const std::string& ext) {
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif"
        || ext == ".bmp" || ext == ".webp";
}

bool IsAudioExtension(const std::string& ext) {
    return ext == ".mp3" || ext == ".wav" || ext == ".m4a" || ext == ".aac"
        || ext == ".ogg" || ext == ".opus";
}

bool IsVideoExtension(const std::string& ext) {
    return ext == ".mp4" || ext == ".mov" || ext == ".webm" || ext == ".mkv";
}

}  // namespace

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

    std::string receive_id = msg.chat_id;
    std::string receive_id_type = "chat_id";
    if (receive_id.empty() && !msg.reply_to.empty()) {
        auto it = message_chat_ids_.find(msg.reply_to);
        if (it != message_chat_ids_.end()) {
            receive_id = it->second;
        }
        auto it_type = message_receive_id_types_.find(msg.reply_to);
        if (it_type != message_receive_id_types_.end()) {
            receive_id_type = it_type->second;
        }
    }
    if (receive_id.empty()) {
        auto it_id = msg.metadata.find("receive_id");
        if (it_id != msg.metadata.end() && !it_id->second.empty()) {
            receive_id = it_id->second;
        }
        auto it_type = msg.metadata.find("receive_id_type");
        if (it_type != msg.metadata.end() && !it_type->second.empty()) {
            receive_id_type = it_type->second;
        }
    }
    if (receive_id.empty()) {
        return;
    }

    if (!msg.content.empty()) {
        const std::string msg_type = "text";
        nlohmann::json body;
        body["text"] = msg.content;

        if (!im_service_->CreateMessage(receive_id, msg_type, body.dump(), receive_id_type)) {
            std::cerr << "[lark] failed to send message"
                      << " receive_id=" << receive_id
                      << " receive_id_type=" << receive_id_type
                      << " msg_type=" << msg_type
                      << " content=[" << msg.content << "]"
                      << std::endl;
        }
    }

    for (const auto& media_path : msg.media) {
        if (media_path.empty()) {
            continue;
        }
        const auto ext = GetExtensionLower(media_path);
        const auto file_name = GetFileName(media_path);
        if (IsImageExtension(ext)) {
            std::string image_key;
            if (!im_service_->UploadImage(media_path, &image_key)) {
                std::cerr << "[lark] failed to upload image"
                          << " path=" << media_path
                          << std::endl;
                continue;
            }
            nlohmann::json content;
            content["image_key"] = image_key;
            if (!im_service_->CreateMessage(receive_id, "image", content.dump(), receive_id_type)) {
                std::cerr << "[lark] failed to send image"
                          << " receive_id=" << receive_id
                          << " receive_id_type=" << receive_id_type
                          << " image_key=" << image_key
                          << " path=" << media_path
                          << std::endl;
            }
            continue;
        }

        const std::string file_type = IsAudioExtension(ext) ? "audio"
                                  : (IsVideoExtension(ext) ? "video" : "file");
        std::string file_key;
        // set file type to ""
        if (!im_service_->UploadFile("", media_path, file_name, &file_key)) {
            std::cerr << "[lark] failed to upload file"
                      << " path=" << media_path
                      << " file_type=" << file_type
                      << std::endl;
            continue;
        }

        bool sent = false;
        if (file_type == "audio") {
            sent = im_service_->SendAudioMessage(receive_id, file_key, receive_id_type);
        } else {
            sent = im_service_->SendFileMessage(receive_id, file_key, receive_id_type);
        }
        if (!sent) {
            std::cerr << "[lark] failed to send file"
                      << " receive_id=" << receive_id
                      << " receive_id_type=" << receive_id_type
                      << " file_type=" << file_type
                      << " file_key=" << file_key
                      << " path=" << media_path
                      << std::endl;
        }
    }
}

void LarkChannel::HandleIncomingMessage(const lark::im::v1::MessageEvent& event) {
    if (event.chat_id.empty() && event.sender_id.empty()) {
        return;
    }

    const bool use_open_id = event.chat_id.empty();
    const std::string receive_id = use_open_id ? event.sender_id : event.chat_id;
    const std::string receive_id_type = use_open_id ? "open_id" : "chat_id";
    const std::string sender_id = event.sender_id.empty() ? receive_id : event.sender_id;
    const std::string chat_id = event.chat_id.empty() ? receive_id : event.chat_id;
    const auto content = ExtractTextFromContent(event.msg_type, event.content);

    std::unordered_map<std::string, std::string> metadata;
    metadata["message_id"] = event.message_id;
    metadata["sender_id"] = sender_id;
    metadata["msg_type"] = event.msg_type;
    metadata["raw_content"] = event.content;
    metadata["chat_id"] = chat_id;
    metadata["receive_id"] = receive_id;
    metadata["receive_id_type"] = receive_id_type;

    if (!event.message_id.empty()) {
        message_chat_ids_[event.message_id] = receive_id;
        message_receive_id_types_[event.message_id] = receive_id_type;
    }

    HandleMessage(sender_id, chat_id, content, {}, metadata);
}

std::string LarkChannel::ExtractTextFromContent(const std::string& msg_type,
                                                const std::string& content) const {
    // std::cout << "[lark] msg_type: " << msg_type << std::endl;
    // std::cout << "[lark] content: " << content << std::endl;
    if (msg_type == "text" || msg_type.empty() ) {
        auto json = nlohmann::json::parse(content, nullptr, false);
        if (!json.is_discarded() && json.contains("text") && json["text"].is_string()) {
            return json["text"].get<std::string>();
        }
    }
    return content;
}

}  // namespace kabot::channels
