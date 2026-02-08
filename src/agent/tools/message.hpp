#pragma once

#include <functional>
#include <string>

#include "agent/tools/tool.hpp"
#include "bus/events.hpp"

namespace kabot::agent::tools {

class MessageTool : public Tool {
public:
    using SendCallback = std::function<void(const kabot::bus::OutboundMessage&)>;

    explicit MessageTool(SendCallback callback = nullptr);

    void SetContext(const std::string& channel, const std::string& chat_id);

    std::string Name() const override { return "message"; }
    std::string Description() const override { return "Send a message to the user."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;

private:
    SendCallback callback_;
    std::string default_channel_;
    std::string default_chat_id_;
};

}  // namespace kabot::agent::tools
