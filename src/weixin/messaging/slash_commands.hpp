#pragma once

#include <string>
#include <optional>

namespace weixin::messaging {

// Handle slash commands like /echo, /toggle-debug
class SlashCommands {
public:
  struct CommandResult {
    bool handled = false;
    std::string response;
    bool toggle_debug = false;
  };
  
  // Parse and handle command
  static CommandResult Handle(const std::string& text, const std::string& user_id);
  
private:
  static CommandResult HandleEcho(const std::string& args);
  static CommandResult HandleToggleDebug(const std::string& user_id);
};

} // namespace weixin::messaging
