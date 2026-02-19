#pragma once

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <vector>

#include <tgbot/tgbot.h>

#include "channels/channel_base.hpp"
#include "config/config_schema.hpp"

namespace kabot::channels {

class TelegramChannel : public ChannelBase {
public:
    TelegramChannel(const kabot::config::TelegramConfig& config,
                    kabot::bus::MessageBus& bus);
    void Start() override;
    void Stop() override;
    void Send(const kabot::bus::OutboundMessage& msg) override;

    void HandleIncomingMessage(
        const std::string& sender_id,
        const std::string& chat_id,
        const std::string& text,
        const std::string& caption,
        const std::string& media_type,
        const std::string& mime_type,
        const std::string& media_id,
        std::unordered_map<std::string, std::string> extra_metadata = {});

private:
    kabot::config::TelegramConfig config_;
    std::unordered_map<std::string, std::string> chat_ids_;
    std::unordered_map<std::string, std::string> message_chat_ids_;
    std::unordered_map<std::string, long long> last_message_ids_;
    std::unique_ptr<TgBot::HttpClient> http_client_;
    std::unique_ptr<TgBot::Bot> bot_;
    std::unique_ptr<TgBot::TgLongPoll> long_poll_;
    std::unique_ptr<std::thread> polling_thread_;
    std::atomic<bool> polling_{false};

    std::string ConvertMarkdownToHtml(const std::string& text) const;
    std::string GetMediaExtension(const std::string& media_type, const std::string& mime_type) const;
    std::string JoinParts(const std::vector<std::string>& parts) const;
    std::string ResolveMediaPath(const std::string& media_id, const std::string& ext) const;
};

}  // namespace kabot::channels
