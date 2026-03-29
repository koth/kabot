#include "storage/state_directory.hpp"

#include <cstdlib>

namespace weixin::storage {

std::filesystem::path GetStateDirectory() {
  const char* state_dir = std::getenv("KABOT_STATE_DIR");
  if (state_dir) {
    return std::filesystem::path(state_dir);
  }
  
  const char* home = std::getenv("HOME");
#ifdef _WIN32
  if (!home) {
    home = std::getenv("USERPROFILE");
  }
#endif
  
  if (home) {
    return std::filesystem::path(home) / ".kabot";
  }
  
  return std::filesystem::path(".");
}

std::filesystem::path GetAccountDirectory() {
  return GetStateDirectory() / "accounts";
}

} // namespace weixin::storage
