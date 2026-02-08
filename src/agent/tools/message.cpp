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
    return R"({"type":"object","properties":{"content":{"type":"string"},"channel":{"type":"string"},"chat_id":{"type":"string"}},"required":["content"]})";
}

std::string MessageTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    const auto content = GetParam(params, "content");
    if (content.empty()) {
        return "Error: content is required";
    }

    const auto channel = GetParam(params, "channel");
    const auto chat_id = GetParam(params, "chat_id");

    kabot::bus::OutboundMessage msg{};
    msg.channel = channel.empty() ? default_channel_ : channel;
    msg.chat_id = chat_id.empty() ? default_chat_id_ : chat_id;
    msg.content = content;

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
