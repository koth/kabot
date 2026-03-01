#include "agent/tools/message.hpp"

namespace kabot::agent::tools {
namespace {

std::string GetParam(const std::unordered_map<std::string, std::string>& params,
                     const std::string& name) {
    auto it = params.find(name);
    if (it == params.end()) {
        return {};
    }
    return it->second;
}

}  // namespace

MessageTool::MessageTool(SendCallback callback)
    : callback_(std::move(callback)) {}

void MessageTool::SetContext(const std::string& channel, const std::string& chat_id) {
    default_channel_ = channel;
    default_chat_id_ = chat_id;
}

std::string MessageTool::ParametersJson() const {
    return R"({"type":"object","properties":{"content":{"type":"string"},"media":{"type":"string","description":"comma-separated local file paths"},"channel":{"type":"string"},"chat_id":{"type":"string"}},"required":[]})";
}

std::string MessageTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    const auto content = GetParam(params, "content");
    const auto media_raw = GetParam(params, "media");

    const auto channel = GetParam(params, "channel");
    const auto chat_id = GetParam(params, "chat_id");

    kabot::bus::OutboundMessage msg{};
    msg.channel = channel.empty() ? default_channel_ : channel;
    msg.chat_id = chat_id.empty() ? default_chat_id_ : chat_id;
    msg.content = content;

    if (!media_raw.empty()) {
        std::size_t start = 0;
        while (start < media_raw.size()) {
            auto end = media_raw.find(',', start);
            if (end == std::string::npos) {
                end = media_raw.size();
            }
            auto token = media_raw.substr(start, end - start);
            auto first = token.find_first_not_of(" \t\r\n");
            if (first != std::string::npos) {
                auto last = token.find_last_not_of(" \t\r\n");
                msg.media.push_back(token.substr(first, last - first + 1));
            }
            start = end + 1;
        }
    }

    if (msg.content.empty() && msg.media.empty()) {
        return "Error: content or media is required";
    }

    if (msg.channel.empty() || msg.chat_id.empty()) {
        return "Error: no target channel/chat_id";
    }

    if (!callback_) {
        return "Error: message callback not configured";
    }

    callback_(msg);
    return "Message sent";
}

}  // namespace kabot::agent::tools
