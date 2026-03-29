#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace weixin::storage {

// Sync buffer persistence for get_updates
class SyncBuffer {
public:
  explicit SyncBuffer(const std::string& account_id);
  
  void Save(const std::string& buffer);
  std::optional<std::string> Load();
  void Clear();
  
private:
  std::filesystem::path file_path_;
};

} // namespace weixin::storage
