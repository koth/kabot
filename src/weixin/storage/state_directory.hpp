#pragma once

#include <cstdint>
#include <filesystem>

namespace weixin::storage {

// Resolve state directory from environment or default
std::filesystem::path GetStateDirectory();
std::filesystem::path GetAccountDirectory();

} // namespace weixin::storage
