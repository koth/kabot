#include "channels/channel_manager.hpp"

#include "channels/lark_channel.hpp"
#include "channels/telegram_channel.hpp"

namespace kabot::channels {

ChannelManager::ChannelManager(const kabot::config::Config& config,
                               kabot::bus::MessageBus& bus)
    : bus_(bus)
    , config_(config) {
    InitChannels();
}

void ChannelManager::InitChannels() {
    RegisterTelegram(config_.channels.telegram);
    RegisterLark(config_.channels.lark);
}

void ChannelManager::Register(std::unique_ptr<ChannelBase> channel) {
    auto name = channel->Name();
    channels_.emplace(std::move(name), std::move(channel));
}

ChannelBase* ChannelManager::GetChannel(const std::string& name) {
    auto it = channels_.find(name);
    if (it == channels_.end()) {
        return nullptr;
    }
    return it->second.get();
}

void ChannelManager::DispatchOutbound(const kabot::bus::OutboundMessage& msg) {
    auto channel = GetChannel(msg.channel);
    if (channel) {
        channel->Send(msg);
    }
}

void ChannelManager::StartAll() {
    if (!dispatch_running_) {
        dispatch_running_ = true;
        dispatch_thread_ = std::thread([this] { RunOutboundDispatcher(); });
    }
    for (auto& [_, channel] : channels_) {
        channel->Start();
    }
}

void ChannelManager::StopAll() {
    dispatch_running_ = false;
    if (dispatch_thread_.joinable()) {
        dispatch_thread_.join();
    }
    for (auto& [_, channel] : channels_) {
        channel->Stop();
    }
}

std::unordered_map<std::string, bool> ChannelManager::Status() const {
    std::unordered_map<std::string, bool> status;
    for (const auto& [name, channel] : channels_) {
        status[name] = channel->IsRunning();
    }
    return status;
}

void ChannelManager::RegisterTelegram(const kabot::config::TelegramConfig& config) {
    if (!config.enabled) {
        return;
    }
    Register(std::make_unique<TelegramChannel>(config, bus_));
}

void ChannelManager::RegisterLark(const kabot::config::LarkConfig& config) {
    if (!config.enabled) {
        return;
    }
    Register(std::make_unique<LarkChannel>(config, bus_));
}

void ChannelManager::RunOutboundDispatcher() {
    while (dispatch_running_) {
        kabot::bus::OutboundMessage msg{};
        if (!bus_.TryConsumeOutbound(msg, std::chrono::milliseconds(1000))) {
            continue;
        }
        auto channel = GetChannel(msg.channel);
        if (channel) {
            channel->Send(msg);
        }
    }
}

}  // namespace kabot::channels
