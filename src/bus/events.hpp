#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

namespace kabot::bus {

struct InboundMessage {
    std::string channel;
    std::string channel_instance;
    std::string agent_name;
    std::string sender_id;
    std::string chat_id;
    std::string content;
    std::vector<std::string> media;
    std::unordered_map<std::string, std::string> metadata;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();

    std::string SessionKey() const {
        const auto& effective_channel = channel_instance.empty() ? channel : channel_instance;
        const auto& effective_agent = agent_name.empty() ? std::string("default") : agent_name;
        return effective_channel + ":" + effective_agent + ":" + chat_id;
    }
};

struct OutboundMessage {
    std::string channel;
    std::string channel_instance;
    std::string agent_name;
    std::string chat_id;
    std::string content;
    std::string reply_to;
    std::vector<std::string> media;
    std::unordered_map<std::string, std::string> metadata;
};

}  // namespace kabot::bus
