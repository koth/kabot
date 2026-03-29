#pragma once

#include "auth/account.hpp"
#include <vector>
#include <optional>

namespace weixin::auth {

// Account manager for loading/saving accounts
class Accounts {
public:
  // Load all accounts from storage
  static std::vector<WeixinAccount> LoadAll();
  
  // Load single account by ID
  static std::optional<WeixinAccount> Load(const std::string& account_id);
  
  // Save account
  static bool Save(const WeixinAccount& account);
  
  // Delete account
  static bool Delete(const std::string& account_id);
  
  // Normalize account ID
  static std::string NormalizeId(const std::string& account_id);
};

} // namespace weixin::auth
