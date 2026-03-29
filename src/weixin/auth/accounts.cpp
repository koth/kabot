#include "auth/accounts.hpp"
#include "storage/state_directory.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace weixin::auth {

std::vector<WeixinAccount> Accounts::LoadAll() {
  std::vector<WeixinAccount> accounts;
  
  auto accounts_dir = storage::GetAccountDirectory();
  if (!std::filesystem::exists(accounts_dir)) {
    return accounts;
  }
  
  // TODO: Iterate through account files and load them
  
  return accounts;
}

std::optional<WeixinAccount> Accounts::Load(const std::string& account_id) {
  auto normalized = NormalizeId(account_id);
  auto file_path = storage::GetAccountDirectory() / (normalized + ".json");
  
  if (!std::filesystem::exists(file_path)) {
    return std::nullopt;
  }
  
  try {
    std::ifstream file(file_path);
    nlohmann::json j;
    file >> j;
    
    WeixinAccount account;
    account.account_id = j.value("account_id", "");
    account.token = j.value("token", "");
    account.name = j.value("name", "");
    account.enabled = j.value("enabled", true);
    account.base_url = j.value("base_url", "https://ilinkai.weixin.qq.com");
    account.cdn_base_url = j.value("cdn_base_url", "https://novac2c.cdn.weixin.qq.com/c2c");
    
    if (j.contains("route_tag")) {
      account.route_tag = j["route_tag"].get<int>();
    }
    
    return account;
  } catch (...) {
    return std::nullopt;
  }
}

bool Accounts::Save(const WeixinAccount& account) {
  auto accounts_dir = storage::GetAccountDirectory();
  std::filesystem::create_directories(accounts_dir);
  
  auto normalized = NormalizeId(account.account_id);
  auto file_path = accounts_dir / (normalized + ".json");
  
  try {
    nlohmann::json j;
    j["account_id"] = account.account_id;
    j["token"] = account.token;
    j["name"] = account.name.value_or("");
    j["enabled"] = account.enabled;
    j["base_url"] = account.base_url;
    j["cdn_base_url"] = account.cdn_base_url;
    if (account.route_tag.has_value()) {
      j["route_tag"] = account.route_tag.value();
    }
    
    std::ofstream file(file_path);
    file << j.dump(2);
    
    return file.good();
  } catch (...) {
    return false;
  }
}

bool Accounts::Delete(const std::string& account_id) {
  auto normalized = NormalizeId(account_id);
  auto file_path = storage::GetAccountDirectory() / (normalized + ".json");
  
  if (!std::filesystem::exists(file_path)) {
    return false;
  }
  
  return std::filesystem::remove(file_path);
}

std::string Accounts::NormalizeId(const std::string& account_id) {
  std::string normalized = account_id;
  
  // Replace special characters
  std::replace_if(normalized.begin(), normalized.end(), 
                  [](char c) {
                    return !std::isalnum(static_cast<unsigned char>(c)) && 
                           c != '-' && c != '_';
                  }, '-');
  
  return normalized;
}

} // namespace weixin::auth
