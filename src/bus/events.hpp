#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

namespace kabot::bus {

struct InboundMessage {
    std::string channel;
    std::string sender_id;
    std::string chat_id;
    std::string content;
    std::vector<std::string> media;
    std::unordered_map<std::string, std::string> metadata;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();

    std::string SessionKey() const {
        return channel + ":" + chat_id;
    }
};

struct OutboundMessage {
    std::string channel;
    std::string chat_id;
    std::string content;
    std::string reply_to;
    std::vector<std::string> media;
    std::unordered_map<std::string, std::string> metadata;
};

}  // namespace kabot::bus
