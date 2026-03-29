#pragma once

#include <nlohmann/json.hpp>
#include <filesystem>
#include <optional>
#include <string>

namespace weixin::storage {

// Context token storage
class ContextTokenStore {
public:
  explicit ContextTokenStore(const std::string& account_id);
  
  void Save(const std::string& user_id, const std::string& token);
  std::optional<std::string> Load(const std::string& user_id);
  void CleanupExpired();
  
private:
  std::string account_id_;
  std::filesystem::path file_path_;
  
  nlohmann::json LoadAll();
  void SaveAll(const nlohmann::json& tokens);
};

} // namespace weixin::storage
