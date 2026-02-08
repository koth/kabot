#include "bus/message_bus.hpp"

#include <iostream>

namespace kabot::bus {

void MessageBus::PublishInbound(const InboundMessage& msg) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        inbound_.push(msg);
    }
    cv_.notify_one();
}

InboundMessage MessageBus::ConsumeInbound() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !inbound_.empty(); });
    auto msg = inbound_.front();
    inbound_.pop();
    return msg;
}

bool MessageBus::TryConsumeInbound(InboundMessage& msg, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this] { return !inbound_.empty(); })) {
        return false;
    }
    msg = inbound_.front();
    inbound_.pop();
    return true;
}

std::size_t MessageBus::InboundSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inbound_.size();
}

void MessageBus::PublishOutbound(const OutboundMessage& msg) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        outbound_.push(msg);
    }
    cv_.notify_one();
}

OutboundMessage MessageBus::ConsumeOutbound() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !outbound_.empty(); });
    auto msg = outbound_.front();
    outbound_.pop();
    return msg;
}

bool MessageBus::TryConsumeOutbound(OutboundMessage& msg, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this] { return !outbound_.empty(); })) {
        return false;
    }
    msg = outbound_.front();
    outbound_.pop();
    return true;
}

std::size_t MessageBus::OutboundSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return outbound_.size();
}

void MessageBus::SubscribeOutbound(const std::string& channel,
                                   std::function<void(const OutboundMessage&)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_[channel].push_back(std::move(callback));
}

void MessageBus::DispatchOutbound() {
    running_ = true;
    while (running_) {
        OutboundMessage msg{};
        if (!TryConsumeOutbound(msg, std::chrono::milliseconds(1000))) {
            continue;
        }
        std::vector<std::function<void(const OutboundMessage&)>> callbacks;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = subscribers_.find(msg.channel);
            if (it != subscribers_.end()) {
                callbacks = it->second;
            }
        }
        for (const auto& cb : callbacks) {
            if (cb) {
                cb(msg);
            }
        }
    }
}

void MessageBus::Stop() {
    running_ = false;
}

}  // namespace kabot::bus
