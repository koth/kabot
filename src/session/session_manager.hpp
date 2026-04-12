#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "providers/llm_provider.hpp"
#include "nlohmann/json.hpp"
#include "sqlite3.h"

namespace kabot::session {

struct SessionMessage {
    std::string role;
    std::string content;
    std::string timestamp;
    std::string name;
    std::string tool_call_id;
    std::vector<kabot::providers::ToolCallRequest> tool_calls;
};

class Session {
public:
    explicit Session(std::string key);
    Session(
        std::string key,
        std::vector<SessionMessage> messages,
        std::string created_at,
        std::string updated_at,
        nlohmann::json metadata);

    void AddMessage(const std::string& role, const std::string& content);
    void AddMessage(const std::string& role,
                    const std::string& content,
                    const std::vector<kabot::providers::ToolCallRequest>& tool_calls);
    void AddToolMessage(const std::string& tool_call_id,
                        const std::string& tool_name,
                        const std::string& content);
    std::vector<kabot::providers::Message> GetHistory(std::size_t max_messages = 50) const;

    const std::string& Key() const { return key_; }
    const std::vector<SessionMessage>& Messages() const { return messages_; }
    const std::string& CreatedAt() const { return created_at_; }
    const std::string& UpdatedAt() const { return updated_at_; }
    const nlohmann::json& Metadata() const { return metadata_; }

    void SetCreatedAt(std::string created_at) { created_at_ = std::move(created_at); }
    void SetUpdatedAt(std::string updated_at) { updated_at_ = std::move(updated_at); }
    void SetMetadata(nlohmann::json metadata) { metadata_ = std::move(metadata); }

private:
    std::string key_;
    std::vector<SessionMessage> messages_;
    std::string created_at_;
    std::string updated_at_;
    nlohmann::json metadata_ = nlohmann::json::object();
};

struct SessionInfo {
    std::string key;
    std::string created_at;
    std::string updated_at;
    std::string path;
};

class SessionManager {
public:
    explicit SessionManager(std::string workspace);
    ~SessionManager();

    Session GetOrCreate(const std::string& key);
    std::optional<Session> Get(const std::string& key);
    void Save(const Session& session);
    bool Delete(const std::string& key);
    std::vector<SessionInfo> ListSessions() const;

private:
    std::optional<Session> Load(const std::string& key) const;
    void EnsureSchema();
    static bool Exec(sqlite3* db, const std::string& sql);
    static std::string SafeText(const unsigned char* text);

    std::string workspace_;
    std::filesystem::path db_path_;
    sqlite3* db_ = nullptr;
    std::unordered_map<std::string, Session> cache_;
    std::mutex mutex_;
};

}  // namespace kabot::session
