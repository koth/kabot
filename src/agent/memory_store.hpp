#pragma once

#include <string>

namespace kabot::agent {

class MemoryStore {
public:
    explicit MemoryStore(std::string workspace);

    std::string GetMemoryContext() const;
    std::string GetRecentMemories(int days = 7) const;
    std::string ReadToday() const;
    void AppendToday(const std::string& content) const;
    std::string ReadLongTerm() const;
    void WriteLongTerm(const std::string& content) const;

private:
    std::string workspace_;
    std::string memory_dir_;

    std::string ReadFileIfExists(const std::string& path) const;
    std::string TodayDate() const;
    std::string DateStringDaysAgo(int days) const;
    std::string MemoryFilePath() const;
    std::string TodayFilePath() const;
};

}  // namespace kabot::agent
