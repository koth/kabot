#include "connection_manager.hpp"
#include <algorithm>
#include <chrono>

namespace weixin::monitor {

ConnectionManager::ConnectionManager() = default;

void ConnectionManager::RecordSuccess() {
  consecutive_failures_ = 0;
  retry_attempt_ = 0;
  in_cooldown_ = false;
  should_pause_ = false;
  cooldown_start_time_ms_ = 0;
}

bool ConnectionManager::RecordFailure(const api::APIError& error, int& output_delay_ms) {
  // Check for session expiration
  if (error.IsSessionExpired()) {
    should_pause_ = true;
    output_delay_ms = kSessionExpirationPauseSec * 1000;
    return false;
  }

  consecutive_failures_++;

  // Check if we've hit max consecutive failures
  if (consecutive_failures_ >= kMaxConsecutiveFailures) {
    in_cooldown_ = true;
    cooldown_start_time_ms_ = GetCurrentTimeMs();
    output_delay_ms = kCooldownPeriodSec * 1000;
    return false;
  }

  // Calculate exponential backoff
  retry_attempt_++;
  output_delay_ms = CalculateBackoffMs();
  return true;
}

bool ConnectionManager::IsHealthy() const {
  return consecutive_failures_ == 0 && !in_cooldown_ && !should_pause_;
}

bool ConnectionManager::ShouldPause() const {
  if (!should_pause_) {
    return false;
  }

  // Check if cooldown period has elapsed for pause state
  if (IsCooldownElapsed()) {
    return false;
  }

  return true;
}

void ConnectionManager::Reset() {
  consecutive_failures_ = 0;
  retry_attempt_ = 0;
  in_cooldown_ = false;
  should_pause_ = false;
  cooldown_start_time_ms_ = 0;
}

int64_t ConnectionManager::GetCurrentTimeMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

int ConnectionManager::CalculateBackoffMs() const {
  // Exponential backoff: initial * 2^(attempt - 1)
  // Cap at maximum retry delay
  int delay = kInitialRetryDelayMs;
  for (int i = 1; i < retry_attempt_ && delay < kMaxRetryDelayMs; ++i) {
    delay *= 2;
  }
  return std::min(delay, kMaxRetryDelayMs);
}

bool ConnectionManager::IsCooldownElapsed() const {
  if (cooldown_start_time_ms_ == 0) {
    return true;
  }

  int64_t now = GetCurrentTimeMs();
  int64_t elapsed_ms = now - cooldown_start_time_ms_;
  int cooldown_duration_ms = in_cooldown_ ? kCooldownPeriodSec * 1000 
                                          : kSessionExpirationPauseSec * 1000;

  return elapsed_ms >= cooldown_duration_ms;
}

} // namespace weixin::monitor
