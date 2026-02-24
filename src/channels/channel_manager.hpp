#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>

#include "channels/channel_base.hpp"
#include "config/config_schema.hpp"

namespace kabot::channels {

class ChannelManager {
public:
    ChannelManager(const kabot::config::Config& config, kabot::bus::MessageBus& bus);

    void Register(std::unique_ptr<ChannelBase> channel);
    ChannelBase* GetChannel(const std::string& name);
    void DispatchOutbound(const kabot::bus::OutboundMessage& msg);
    void StartAll();
    void StopAll();

    std::unordered_map<std::string, bool> Status() const;

private:
    void InitChannels();
    void RegisterTelegram(const kabot::config::TelegramConfig& config);
    void RegisterLark(const kabot::config::LarkConfig& config);
    void RunOutboundDispatcher();

    kabot::bus::MessageBus& bus_;
    kabot::config::Config config_;
    std::atomic<bool> dispatch_running_{false};
    std::thread dispatch_thread_;

private:
    std::unordered_map<std::string, std::unique_ptr<ChannelBase>> channels_;
};

}  // namespace kabot::channels
