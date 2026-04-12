#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace kabot {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads) {
        workers_.reserve(num_threads);
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) {
                            return;
                        }
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    {
                        std::unique_lock<std::mutex> lock(active_mutex_);
                        ++active_count_;
                    }
                    task();
                    {
                        std::unique_lock<std::mutex> lock(active_mutex_);
                        --active_count_;
                    }
                    active_cv_.notify_one();
                }
            });
        }
    }

    ~ThreadPool() {
        Shutdown();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template <typename F>
    void Submit(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                return;
            }
            tasks_.emplace(std::forward<F>(f));
        }
        condition_.notify_one();
    }

    std::size_t PendingCount() const {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }

    bool HasPending() const {
        return PendingCount() > 0;
    }

    void Shutdown() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    template <typename Rep, typename Period>
    bool WaitForEmpty(std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock<std::mutex> lock(active_mutex_);
        return active_cv_.wait_for(lock, timeout, [this] { return active_count_ == 0; });
    }

private:
    std::vector<std::thread> workers_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::queue<std::function<void()>> tasks_;
    bool stop_ = false;

    mutable std::mutex active_mutex_;
    std::condition_variable active_cv_;
    std::size_t active_count_ = 0;
};

}  // namespace kabot
