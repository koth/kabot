#include "storage/context_token_store.hpp"
#include "storage/state_directory.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <chrono>

namespace weixin::storage {

// Token expiration: 24 hours
constexpr auto kTokenExpirationHours = 24;

ContextTokenStore::ContextTokenStore(const std::string& account_id) 
    : account_id_(account_id) {
  file_path_ = GetStateDirectory() / (account_id + ".context-tokens.json");
}

void ContextTokenStore::Save(const std::string& user_id, const std::string& token) {
  std::filesystem::create_directories(file_path_.parent_path());
  
  nlohmann::json tokens = LoadAll();
  
  nlohmann::json entry;
  entry["token"] = token;
  auto now = std::chrono::system_clock::now();
  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  entry["timestamp"] = seconds;
  
  tokens[user_id] = entry;
  
  SaveAll(tokens);
}

void ContextTokenStore::SaveAll(const nlohmann::json& tokens) {
  std::ofstream file(file_path_);
  file << tokens.dump(2);
}

std::optional<std::string> ContextTokenStore::Load(const std::string& user_id) {
  CleanupExpired();
  
  nlohmann::json tokens = LoadAll();
  
  if (tokens.contains(user_id)) {
    auto& entry = tokens[user_id];
    if (entry.contains("token") && entry["token"].is_string()) {
      return entry["token"].get<std::string>();
    }
  }
  
  return std::nullopt;
}

void ContextTokenStore::CleanupExpired() {
  nlohmann::json tokens = LoadAll();
  nlohmann::json cleaned;
  
  auto now = std::chrono::system_clock::now();
  
  for (auto& [user_id, entry] : tokens.items()) {
    if (entry.contains("timestamp")) {
      auto timestamp_sec = entry["timestamp"].get<int64_t>();
      auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
          now.time_since_epoch()).count();
      auto diff_hours = (now_sec - timestamp_sec) / 3600;
      if (diff_hours < kTokenExpirationHours) {
        cleaned[user_id] = entry;
      }
    }
  }
  
  SaveAll(cleaned);
}

nlohmann::json ContextTokenStore::LoadAll() {
  if (!std::filesystem::exists(file_path_)) {
    return nlohmann::json::object();
  }
  
  try {
    std::ifstream file(file_path_);
    nlohmann::json j;
    file >> j;
    return j;
  } catch (...) {
    return nlohmann::json::object();
  }
}

} // namespace weixin::storage
