#pragma once

#include "api/api_types.hpp"
#include "messaging/inbound_processor.hpp"
#include <string>
#include <functional>

namespace weixin::messaging {

// Message processing pipeline
class MessagePipeline {
public:
  using MessageHandler = std::function<void(const InboundProcessor::ProcessedMessage&)>;
  
  MessagePipeline(const std::string& account_id,
                  const std::string& media_output_dir);
  
  // Process a message through the pipeline
  void Process(const api::WeixinMessage& msg);
  
  // Set handler for processed messages
  void SetHandler(MessageHandler handler);
  
  // Set debug mode
  void SetDebugMode(bool enabled);
  
private:
  std::string account_id_;
  std::string media_output_dir_;
  InboundProcessor processor_;
  MessageHandler handler_;
  bool debug_mode_ = false;
  
  void HandleSlashCommand(const InboundProcessor::ProcessedMessage& msg);
  void LogMessage(const api::WeixinMessage& msg);
};

} // namespace weixin::messaging
