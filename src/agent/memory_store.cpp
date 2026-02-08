#include "agent/memory_store.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kabot::agent {

MemoryStore::MemoryStore(std::string workspace)
    : workspace_(std::move(workspace))
    , memory_dir_(workspace_ + "/memory") {
    std::filesystem::create_directories(memory_dir_);
}

std::string MemoryStore::GetMemoryContext() const {
    std::ostringstream oss;

    const auto long_term = ReadLongTerm();
    if (!long_term.empty()) {
        oss << "## Long-term Memory\n" << long_term << "\n\n";
    }

    const auto today = ReadToday();
    if (!today.empty()) {
        oss << "## Today's Notes\n" << today << "\n";
    }

    return oss.str();
}

std::string MemoryStore::ReadToday() const {
    return ReadFileIfExists(TodayFilePath());
}

void MemoryStore::AppendToday(const std::string& content) const {
    const auto path = TodayFilePath();
    std::string updated = content;
    if (std::filesystem::exists(path)) {
        const auto existing = ReadFileIfExists(path);
        if (!existing.empty()) {
            updated = existing + "\n" + content;
        }
    } else {
        const auto header = "# " + TodayDate() + "\n\n";
        updated = header + content;
    }
    std::ofstream file(path, std::ios::trunc);
    if (file.is_open()) {
        file << updated;
    }
}

std::string MemoryStore::ReadLongTerm() const {
    return ReadFileIfExists(MemoryFilePath());
}

void MemoryStore::WriteLongTerm(const std::string& content) const {
    std::ofstream file(MemoryFilePath(), std::ios::trunc);
    if (file.is_open()) {
        file << content;
    }
}

std::string MemoryStore::GetRecentMemories(int days) const {
    std::ostringstream oss;
    bool has_content = false;
    for (int i = 0; i < days; ++i) {
        const auto date = DateStringDaysAgo(i);
        const auto content = ReadFileIfExists(memory_dir_ + "/" + date + ".md");
        if (content.empty()) {
            continue;
        }
        if (has_content) {
            oss << "\n\n---\n\n";
        }
        oss << content;
        has_content = true;
    }
    return oss.str();
}

std::string MemoryStore::ReadFileIfExists(const std::string& path) const {
    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string MemoryStore::TodayDate() const {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif
    char buffer[11];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &local_time);
    return std::string(buffer);
}

std::string MemoryStore::MemoryFilePath() const {
    return memory_dir_ + "/MEMORY.md";
}

std::string MemoryStore::TodayFilePath() const {
    return memory_dir_ + "/" + TodayDate() + ".md";
}

std::string MemoryStore::DateStringDaysAgo(int days) const {
    const auto now = std::chrono::system_clock::now() - std::chrono::hours(24 * days);
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif
    char buffer[11];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &local_time);
    return std::string(buffer);
}

}  // namespace kabot::agent
