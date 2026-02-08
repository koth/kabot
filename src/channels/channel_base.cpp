#include "channels/channel_base.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace kabot::channels {

ChannelBase::ChannelBase(std::string name,
                         kabot::bus::MessageBus& bus,
                         std::vector<std::string> allow_from)
    : name_(std::move(name))
    , bus_(bus)
    , allow_from_(std::move(allow_from)) {}

bool ChannelBase::IsAllowed(const std::string& sender_id) const {
    if (allow_from_.empty()) {
        return true;
    }
    if (std::find(allow_from_.begin(), allow_from_.end(), sender_id) != allow_from_.end()) {
        return true;
    }
    const auto pipe = sender_id.find('|');
    if (pipe != std::string::npos) {
        std::stringstream ss(sender_id);
        std::string part;
        while (std::getline(ss, part, '|')) {
            if (!part.empty() && std::find(allow_from_.begin(), allow_from_.end(), part) != allow_from_.end()) {
                return true;
            }
        }
    }
    return false;
}

void ChannelBase::HandleMessage(
    const std::string& sender_id,
    const std::string& chat_id,
    const std::string& content,
    const std::vector<std::string>& media,
    const std::unordered_map<std::string, std::string>& metadata) {
    if (!IsAllowed(sender_id)) {
        std::cerr << "[channel] message blocked by allow_from: " << sender_id << std::endl;
        return;
    }
    kabot::bus::InboundMessage msg{};
    msg.channel = name_;
    msg.sender_id = sender_id;
    msg.chat_id = chat_id;
    msg.content = content;
    msg.media = media;
    msg.metadata = metadata;
    bus_.PublishInbound(msg);
}

}  // namespace kabot::channels
