#pragma once

#include <string>

#include "agent/tools/tool.hpp"

namespace kabot::agent::tools {

class EdgeTtsTool : public Tool {
public:
    explicit EdgeTtsTool(std::string workspace);

    std::string Name() const override { return "tts"; }
    std::string Description() const override { return "Synthesize speech using Edge TTS."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;

private:
    std::string workspace_;
};

}  // namespace kabot::agent::tools
