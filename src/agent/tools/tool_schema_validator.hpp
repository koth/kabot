#pragma once

#include <string>
#include <unordered_map>

namespace kabot::agent::tools {

class Tool;

std::string ValidateToolInput(const Tool* tool,
                               const std::unordered_map<std::string, std::string>& params);

}  // namespace kabot::agent::tools
