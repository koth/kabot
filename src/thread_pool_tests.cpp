#include "utils/thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[thread_pool_tests] " << message << std::endl;
        std::exit(1);
    }
}

void TestPoolExecutesTasks() {
    kabot::ThreadPool pool(2);
    std::atomic<int> counter{0};
    for (int i = 0; i < 5; ++i) {
        pool.Submit([&counter] { counter.fetch_add(1); });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    pool.WaitForEmpty(std::chrono::seconds(5));
    Expect(counter.load() == 5, "expected all 5 tasks to execute");
}

void TestPoolCapacityBlocking() {
    kabot::ThreadPool pool(1);
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    pool.Submit([&started, &finished] {
        started.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        finished.store(true);
    });
    // Wait until the first task definitely started
    while (!started.load()) {
        std::this_thread::yield();
    }
    // Submit another task while the single worker is busy
    std::atomic<bool> second_finished{false};
    pool.Submit([&second_finished] { second_finished.store(true); });
    // Immediately the second task should be queued but not finished
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Expect(!second_finished.load(), "expected second task to be queued while worker is busy");
    pool.WaitForEmpty(std::chrono::seconds(5));
    Expect(finished.load() && second_finished.load(), "expected both tasks to finish");
}

void TestPoolGracefulShutdown() {
    std::atomic<int> counter{0};
    {
        kabot::ThreadPool pool(2);
        for (int i = 0; i < 3; ++i) {
            pool.Submit([&counter] { counter.fetch_add(1); });
        }
        pool.Shutdown();
    }
    Expect(counter.load() == 3, "expected all tasks to complete after shutdown");
}

}  // namespace

int main() {
    TestPoolExecutesTasks();
    TestPoolCapacityBlocking();
    TestPoolGracefulShutdown();
    std::cout << "thread_pool_tests passed" << std::endl;
    return 0;
}
