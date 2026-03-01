#include "session/session_manager.hpp"

#include <filesystem>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <iostream>

namespace kabot::session {
namespace {

std::string NowIso() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local_time, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

std::vector<kabot::providers::ToolCallRequest> ParseToolCalls(const std::string& text) {
    std::vector<kabot::providers::ToolCallRequest> tool_calls;
    if (text.empty()) {
        return tool_calls;
    }
    auto json = nlohmann::json::parse(text, nullptr, false);
    if (!json.is_array()) {
        return tool_calls;
    }
    for (const auto& entry : json) {
        kabot::providers::ToolCallRequest call{};
        call.id = entry.value("id", "");
        call.name = entry.value("name", "");
        if (entry.contains("arguments") && entry["arguments"].is_object()) {
            for (const auto& item : entry["arguments"].items()) {
                if (item.value().is_string()) {
                    call.arguments[item.key()] = item.value().get<std::string>();
                } else {
                    call.arguments[item.key()] = item.value().dump();
                }
            }
        }
        tool_calls.push_back(std::move(call));
    }
    return tool_calls;
}

std::string SerializeToolCalls(const std::vector<kabot::providers::ToolCallRequest>& tool_calls) {
    if (tool_calls.empty()) {
        return "";
    }
    nlohmann::json json = nlohmann::json::array();
    for (const auto& call : tool_calls) {
        nlohmann::json args = nlohmann::json::object();
        for (const auto& [key, value] : call.arguments) {
            args[key] = value;
        }
        json.push_back({
            {"id", call.id},
            {"name", call.name},
            {"arguments", std::move(args)}
        });
    }
    return json.dump();
}

}  // namespace

Session::Session(std::string key)
    : key_(std::move(key))
    , created_at_(NowIso())
    , updated_at_(created_at_) {}

Session::Session(
    std::string key,
    std::vector<SessionMessage> messages,
    std::string created_at,
    std::string updated_at,
    nlohmann::json metadata)
    : key_(std::move(key))
    , messages_(std::move(messages))
    , created_at_(std::move(created_at))
    , updated_at_(std::move(updated_at))
    , metadata_(std::move(metadata)) {}

void Session::AddMessage(const std::string& role, const std::string& content) {
    messages_.push_back(SessionMessage{role, content, NowIso()});
    updated_at_ = NowIso();
}

void Session::AddMessage(const std::string& role,
                         const std::string& content,
                         const std::vector<kabot::providers::ToolCallRequest>& tool_calls) {
    SessionMessage msg{role, content, NowIso()};
    msg.tool_calls = tool_calls;
    messages_.push_back(std::move(msg));
    updated_at_ = NowIso();
}

void Session::AddToolMessage(const std::string& tool_call_id,
                             const std::string& tool_name,
                             const std::string& content) {
    SessionMessage msg{"tool", content, NowIso()};
    msg.tool_call_id = tool_call_id;
    msg.name = tool_name;
    messages_.push_back(std::move(msg));
    updated_at_ = NowIso();
}

std::vector<kabot::providers::Message> Session::GetHistory(std::size_t max_messages) const {
    std::vector<kabot::providers::Message> history;
    const auto start = messages_.size() > max_messages ? messages_.size() - max_messages : 0;
    std::size_t cutoff = start;
    std::size_t user_seen = 0;
    for (std::size_t i = messages_.size(); i-- > start;) {
        if (messages_[i].role == "user") {
            user_seen++;
            if (user_seen == 3) {
                cutoff = i;
                break;
            }
        }
    }
    for (std::size_t i = start; i < messages_.size(); ++i) {
        const auto& entry = messages_[i];
        const bool allow_tool = i >= cutoff;
        if (entry.role == "tool" && !allow_tool) {
            continue;
        }
        kabot::providers::Message msg{};
        msg.role = entry.role;
        msg.content = entry.content;
        msg.name = entry.name;
        msg.tool_call_id = entry.tool_call_id;
        msg.tool_calls = entry.tool_calls;
        if (!allow_tool && !msg.tool_calls.empty()) {
            msg.tool_calls.clear();
        }
        history.push_back(msg);
    }
    return history;
}

SessionManager::SessionManager(std::string workspace)
    : workspace_(std::move(workspace))
    , db_path_(std::filesystem::path(workspace_) / "sessions.db") {
    EnsureSchema();
}

SessionManager::~SessionManager() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

Session SessionManager::GetOrCreate(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second;
    }
    auto loaded = Load(key);
    if (loaded.has_value()) {
        cache_.emplace(key, *loaded);
        return *loaded;
    }
    Session session(key);
    cache_.emplace(key, session);
    return session;
}

std::optional<Session> SessionManager::Get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second;
    }
    auto loaded = Load(key);
    if (loaded.has_value()) {
        cache_.emplace(key, *loaded);
    }
    return loaded;
}

void SessionManager::Save(const Session& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return;
    }
    Exec(db_, "BEGIN TRANSACTION;");
    sqlite3_stmt* stmt = nullptr;
    const std::string upsert_sql =
        "INSERT INTO sessions(key, created_at, updated_at, metadata) VALUES(?, ?, ?, ?) "
        "ON CONFLICT(key) DO UPDATE SET created_at=excluded.created_at, "
        "updated_at=excluded.updated_at, metadata=excluded.metadata;";
    if (sqlite3_prepare_v2(db_, upsert_sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        const auto meta_text = session.Metadata().dump();
        sqlite3_bind_text(stmt, 1, session.Key().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, session.CreatedAt().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, session.UpdatedAt().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, meta_text.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);

    const std::string delete_sql = "DELETE FROM messages WHERE session_key = ?;";
    if (sqlite3_prepare_v2(db_, delete_sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session.Key().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);

    const std::string insert_sql =
        "INSERT INTO messages(session_key, role, content, timestamp, name, tool_call_id, tool_calls) "
        "VALUES(?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, insert_sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        for (const auto& msg : session.Messages()) {
            const auto tool_calls_text = SerializeToolCalls(msg.tool_calls);
            sqlite3_bind_text(stmt, 1, session.Key().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, msg.role.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, msg.content.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, msg.timestamp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, msg.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, msg.tool_call_id.c_str(), -1, SQLITE_TRANSIENT);
            if (tool_calls_text.empty()) {
                sqlite3_bind_null(stmt, 7);
            } else {
                sqlite3_bind_text(stmt, 7, tool_calls_text.c_str(), -1, SQLITE_TRANSIENT);
            }
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
        }
    }
    sqlite3_finalize(stmt);
    Exec(db_, "COMMIT;");
    cache_.insert_or_assign(session.Key(), session);
}

bool SessionManager::Delete(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.erase(key);
    if (!db_) {
        return false;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM messages WHERE session_key = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
    if (sqlite3_prepare_v2(db_, "DELETE FROM sessions WHERE key = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
    return true;
}

std::vector<SessionInfo> SessionManager::ListSessions() const {
    std::vector<SessionInfo> sessions;
    if (!db_) {
        return sessions;
    }
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = "SELECT key, created_at, updated_at FROM sessions ORDER BY updated_at DESC;";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return sessions;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SessionInfo info{};
        info.key = SafeText(sqlite3_column_text(stmt, 0));
        info.created_at = SafeText(sqlite3_column_text(stmt, 1));
        info.updated_at = SafeText(sqlite3_column_text(stmt, 2));
        info.path = db_path_.string();
        sessions.push_back(std::move(info));
    }
    sqlite3_finalize(stmt);
    return sessions;
}

std::optional<Session> SessionManager::Load(const std::string& key) const {
    if (!db_) {
        return std::nullopt;
    }
    sqlite3_stmt* stmt = nullptr;
    const std::string session_sql = "SELECT created_at, updated_at, metadata FROM sessions WHERE key = ?;";
    if (sqlite3_prepare_v2(db_, session_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }
    std::string created_at = SafeText(sqlite3_column_text(stmt, 0));
    std::string updated_at = SafeText(sqlite3_column_text(stmt, 1));
    std::string metadata_text = SafeText(sqlite3_column_text(stmt, 2));
    sqlite3_finalize(stmt);

    nlohmann::json metadata = nlohmann::json::object();
    if (!metadata_text.empty()) {
        auto parsed = nlohmann::json::parse(metadata_text, nullptr, false);
        if (parsed.is_object()) {
            metadata = std::move(parsed);
        }
    }

    std::vector<SessionMessage> messages;
    const std::string message_sql =
        "SELECT role, content, timestamp, name, tool_call_id, tool_calls "
        "FROM messages WHERE session_key = ? ORDER BY id ASC;";
    if (sqlite3_prepare_v2(db_, message_sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            SessionMessage msg{};
            msg.role = SafeText(sqlite3_column_text(stmt, 0));
            msg.content = SafeText(sqlite3_column_text(stmt, 1));
            msg.timestamp = SafeText(sqlite3_column_text(stmt, 2));
            msg.name = SafeText(sqlite3_column_text(stmt, 3));
            msg.tool_call_id = SafeText(sqlite3_column_text(stmt, 4));
            msg.tool_calls = ParseToolCalls(SafeText(sqlite3_column_text(stmt, 5)));
            messages.push_back(std::move(msg));
        }
    }
    sqlite3_finalize(stmt);

    if (created_at.empty()) {
        created_at = NowIso();
    }
    if (updated_at.empty()) {
        updated_at = created_at;
    }
    Session session(key, std::move(messages), std::move(created_at), std::move(updated_at), std::move(metadata));
    return session;
}

void SessionManager::EnsureSchema() {
    if (db_) {
        return;
    }
    if (sqlite3_open(db_path_.string().c_str(), &db_) != SQLITE_OK) {
        std::cerr << "[session] failed to open sqlite db: " << db_path_ << std::endl;
        sqlite3_close(db_);
        db_ = nullptr;
        return;
    }
    Exec(db_, "PRAGMA journal_mode=WAL;");
    Exec(db_, "CREATE TABLE IF NOT EXISTS sessions ("
             "key TEXT PRIMARY KEY,"
             "created_at TEXT,"
             "updated_at TEXT,"
             "metadata TEXT"
             ");");
    Exec(db_, "CREATE TABLE IF NOT EXISTS messages ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "session_key TEXT,"
             "role TEXT,"
             "content TEXT,"
             "timestamp TEXT,"
             "name TEXT,"
             "tool_call_id TEXT,"
             "tool_calls TEXT"
             ");");
    Exec(db_, "CREATE INDEX IF NOT EXISTS idx_messages_session ON messages(session_key);");
}

bool SessionManager::Exec(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    const auto rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        if (err) {
            std::cerr << "[session] sqlite exec error: " << err << std::endl;
            sqlite3_free(err);
        }
        return false;
    }
    return true;
}

std::string SessionManager::SafeText(const unsigned char* text) {
    return text ? reinterpret_cast<const char*>(text) : std::string();
}

}  // namespace kabot::session
