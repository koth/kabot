#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstddef>
#include <chrono>
#include <atomic>
#include <functional>
#include <unordered_map>

#include "bus/events.hpp"

namespace kabot::bus {

class MessageBus {
public:
    void PublishInbound(const InboundMessage& msg);
    InboundMessage ConsumeInbound();
    bool TryConsumeInbound(InboundMessage& msg, std::chrono::milliseconds timeout);
    std::size_t InboundSize() const;
    void PublishOutbound(const OutboundMessage& msg);
    OutboundMessage ConsumeOutbound();
    bool TryConsumeOutbound(OutboundMessage& msg, std::chrono::milliseconds timeout);
    std::size_t OutboundSize() const;
    void SubscribeOutbound(const std::string& channel,
                           std::function<void(const OutboundMessage&)> callback);
    void DispatchOutbound();
    void Stop();

private:
    std::queue<InboundMessage> inbound_;
    std::queue<OutboundMessage> outbound_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<std::string, std::vector<std::function<void(const OutboundMessage&)>>> subscribers_;
    std::atomic<bool> running_{false};
};

}  // namespace kabot::bus
