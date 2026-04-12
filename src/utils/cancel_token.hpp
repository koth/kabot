#pragma once

#include <atomic>

namespace kabot {

class CancelToken {
public:
    void Cancel() { cancelled_.store(true, std::memory_order_relaxed); }
    bool IsCancelled() const { return cancelled_.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> cancelled_{false};
};

}  // namespace kabot
