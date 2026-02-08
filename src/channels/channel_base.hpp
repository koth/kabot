#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "bus/events.hpp"
#include "bus/message_bus.hpp"

namespace kabot::channels {

class ChannelBase {
public:
    ChannelBase(std::string name,
                kabot::bus::MessageBus& bus,
                std::vector<std::string> allow_from);
    virtual ~ChannelBase() = default;
    virtual std::string Name() const { return name_; }
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Send(const kabot::bus::OutboundMessage& msg) = 0;

    bool IsAllowed(const std::string& sender_id) const;
    void HandleMessage(
        const std::string& sender_id,
        const std::string& chat_id,
        const std::string& content,
        const std::vector<std::string>& media,
        const std::unordered_map<std::string, std::string>& metadata);

    bool IsRunning() const { return running_; }

protected:
    std::string name_;
    kabot::bus::MessageBus& bus_;
    std::vector<std::string> allow_from_;
    bool running_ = false;
};

}  // namespace kabot::channels
