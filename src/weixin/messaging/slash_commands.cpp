#include "messaging/slash_commands.hpp"

#include <sstream>
#include <algorithm>

namespace weixin::messaging {

SlashCommands::CommandResult SlashCommands::Handle(
    const std::string& text, 
    const std::string& user_id) {
  
  if (text.empty() || text[0] != '/') {
    return {false, "", false};
  }
  
  // Parse command
  std::istringstream iss(text);
  std::string command;
  iss >> command;
  
  // Get arguments (rest of the line)
  std::string args;
  std::getline(iss, args);
  // Trim leading space
  if (!args.empty() && args[0] == ' ') {
    args = args.substr(1);
  }
  
  // Convert command to lowercase
  std::transform(command.begin(), command.end(), command.begin(), 
                 [](unsigned char c) { return std::tolower(c); });
  
  if (command == "/echo") {
    return HandleEcho(args);
  } else if (command == "/toggle-debug") {
    return HandleToggleDebug(user_id);
  }
  
  return {false, "", false};
}

SlashCommands::CommandResult SlashCommands::HandleEcho(const std::string& args) {
  return {true, args, false};
}

SlashCommands::CommandResult SlashCommands::HandleToggleDebug(const std::string& user_id) {
  // TODO: Toggle debug mode in storage
  return {true, "Debug mode toggled", true};
}

} // namespace weixin::messaging
