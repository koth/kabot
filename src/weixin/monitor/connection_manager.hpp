#pragma once

#include "../api/api_types.hpp"
#include <cstdint>

namespace weixin::monitor {

// Connection state tracking and reconnection logic
class ConnectionManager {
public:
  // Consecutive failures before entering cooldown
  static constexpr int kMaxConsecutiveFailures = 3;
  // Initial retry delay in milliseconds
  static constexpr int kInitialRetryDelayMs = 1000;
  // Maximum retry delay in milliseconds  
  static constexpr int kMaxRetryDelayMs = 30000;
  // Cooldown period after max failures (seconds)
  static constexpr int kCooldownPeriodSec = 30;
  // Session expiration pause (seconds)
  static constexpr int kSessionExpirationPauseSec = 3600;

  ConnectionManager();

  // Record a successful operation
  void RecordSuccess();
  
  // Record a failure, returns whether to retry
  // output_delay_ms: set to recommended delay before retry
  bool RecordFailure(const api::APIError& error, int& output_delay_ms);
  
  // Check if connection is healthy
  bool IsHealthy() const;
  
  // Check if should pause (session expired, etc.)
  bool ShouldPause() const;
  
  // Get current consecutive failure count
  int GetConsecutiveFailures() const { return consecutive_failures_; }
  
  // Check if in cooldown period
  bool IsInCooldown() const { return in_cooldown_; }
  
  // Reset all counters
  void Reset();

private:
  int consecutive_failures_ = 0;
  int retry_attempt_ = 0;
  bool in_cooldown_ = false;
  bool should_pause_ = false;
  int64_t cooldown_start_time_ms_ = 0;
  
  // Get current time in milliseconds
  static int64_t GetCurrentTimeMs();
  
  // Calculate exponential backoff delay
  int CalculateBackoffMs() const;
  
  // Check if cooldown period has elapsed
  bool IsCooldownElapsed() const;
};

} // namespace weixin::monitor
