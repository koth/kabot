#include "storage/sync_buffer.hpp"
#include "storage/state_directory.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

namespace weixin::storage {

SyncBuffer::SyncBuffer(const std::string& account_id) {
  file_path_ = GetStateDirectory() / (account_id + ".sync.json");
}

void SyncBuffer::Save(const std::string& buffer) {
  std::filesystem::create_directories(file_path_.parent_path());
  
  nlohmann::json j;
  j["buffer"] = buffer;
  j["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
  
  std::ofstream file(file_path_);
  file << j.dump(2);
}

std::optional<std::string> SyncBuffer::Load() {
  if (!std::filesystem::exists(file_path_)) {
    return std::nullopt;
  }
  
  try {
    std::ifstream file(file_path_);
    nlohmann::json j;
    file >> j;
    
    if (j.contains("buffer") && j["buffer"].is_string()) {
      return j["buffer"].get<std::string>();
    }
  } catch (...) {
    // Return empty on error
  }
  
  return std::nullopt;
}

void SyncBuffer::Clear() {
  std::filesystem::remove(file_path_);
}

} // namespace weixin::storage
