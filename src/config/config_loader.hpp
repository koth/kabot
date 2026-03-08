#pragma once

#include <filesystem>
#include <vector>

#include "config/config_schema.hpp"

namespace kabot::config {

Config LoadConfig();
Config LoadConfig(const std::filesystem::path& config_path);
std::vector<std::string> ValidateConfig(const Config& config);

}  // namespace kabot::config
