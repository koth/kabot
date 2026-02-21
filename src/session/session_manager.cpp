#include "session/session_manager.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>

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

std::string SafeKey(const std::string& key) {
    auto safe = key;
    for (auto& ch : safe) {
        if (ch == ':' || ch == '/') {
            ch = '_';
        }
    }
    return safe;
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
    , sessions_dir_(HomePath() / ".kabot" / "sessions") {
    std::filesystem::create_directories(sessions_dir_);
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

void SessionManager::Save(const Session& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto path = SessionPath(session.Key());
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return;
    }
    nlohmann::json meta_line = {
        {"_type", "metadata"},
        {"created_at", session.CreatedAt()},
        {"updated_at", session.UpdatedAt()},
        {"metadata", session.Metadata()}
    };
    file << meta_line.dump() << "\n";
    for (const auto& msg : session.Messages()) {
        nlohmann::json line = {
            {"role", msg.role},
            {"content", msg.content},
            {"timestamp", msg.timestamp}
        };
        if (!msg.name.empty()) {
            line["name"] = msg.name;
        }
        if (!msg.tool_call_id.empty()) {
            line["tool_call_id"] = msg.tool_call_id;
        }
        if (!msg.tool_calls.empty()) {
            nlohmann::json calls = nlohmann::json::array();
            for (const auto& call : msg.tool_calls) {
                nlohmann::json args = nlohmann::json::object();
                for (const auto& [key, value] : call.arguments) {
                    args[key] = value;
                }
                calls.push_back({
                    {"id", call.id},
                    {"name", call.name},
                    {"arguments", args}
                });
            }
            line["tool_calls"] = std::move(calls);
        }
        file << line.dump() << "\n";
    }
    cache_.insert_or_assign(session.Key(), session);
}

bool SessionManager::Delete(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.erase(key);
    const auto path = SessionPath(key);
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
        return true;
    }
    return false;
}

std::vector<SessionInfo> SessionManager::ListSessions() const {
    std::vector<SessionInfo> sessions;
    if (!std::filesystem::exists(sessions_dir_)) {
        return sessions;
    }
    for (const auto& entry : std::filesystem::directory_iterator(sessions_dir_)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".jsonl") {
            continue;
        }
        std::ifstream file(entry.path());
        if (!file.is_open()) {
            continue;
        }
        std::string line;
        if (!std::getline(file, line)) {
            continue;
        }
        try {
            auto data = nlohmann::json::parse(line);
            if (!data.is_object() || data.value("_type", "") != "metadata") {
                continue;
            }
            SessionInfo info{};
            info.key = entry.path().stem().string();
            std::replace(info.key.begin(), info.key.end(), '_', ':');
            info.created_at = data.value("created_at", "");
            info.updated_at = data.value("updated_at", "");
            info.path = entry.path().string();
            sessions.push_back(std::move(info));
        } catch (...) {
            continue;
        }
    }
    std::sort(sessions.begin(), sessions.end(), [](const SessionInfo& a, const SessionInfo& b) {
        return a.updated_at > b.updated_at;
    });
    return sessions;
}

std::optional<Session> SessionManager::Load(const std::string& key) const {
    const auto path = SessionPath(key);
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::vector<SessionMessage> messages;
    nlohmann::json metadata = nlohmann::json::object();
    std::string created_at;
    std::string updated_at;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            auto data = nlohmann::json::parse(line);
            if (data.is_object() && data.value("_type", "") == "metadata") {
                created_at = data.value("created_at", "");
                updated_at = data.value("updated_at", "");
                if (data.contains("metadata")) {
                    metadata = data["metadata"];
                }
                continue;
            }
            if (!data.is_object()) {
                continue;
            }
            SessionMessage msg{};
            msg.role = data.value("role", "");
            msg.content = data.value("content", "");
            msg.timestamp = data.value("timestamp", "");
            msg.name = data.value("name", "");
            msg.tool_call_id = data.value("tool_call_id", "");
            if (data.contains("tool_calls") && data["tool_calls"].is_array()) {
                for (const auto& item : data["tool_calls"]) {
                    kabot::providers::ToolCallRequest call{};
                    call.id = item.value("id", "");
                    call.name = item.value("name", "");
                    if (item.contains("arguments")) {
                        for (const auto& arg : item["arguments"].items()) {
                            if (arg.value().is_string()) {
                                call.arguments[arg.key()] = arg.value().get<std::string>();
                            } else {
                                call.arguments[arg.key()] = arg.value().dump();
                            }
                        }
                    }
                    msg.tool_calls.push_back(std::move(call));
                }
            }
            if (!msg.role.empty()) {
                messages.push_back(std::move(msg));
            }
        } catch (...) {
            continue;
        }
    }
    if (created_at.empty()) {
        created_at = NowIso();
    }
    if (updated_at.empty()) {
        updated_at = created_at;
    }
    return Session(key, std::move(messages), std::move(created_at), std::move(updated_at), std::move(metadata));
}

std::filesystem::path SessionManager::SessionPath(const std::string& key) const {
    const auto safe = SafeKey(key);
    return sessions_dir_ / (safe + ".jsonl");
}

std::filesystem::path SessionManager::HomePath() {
    const char* home = std::getenv("HOME");
#if defined(_WIN32)
    if (!home) {
        home = std::getenv("USERPROFILE");
    }
#endif
    return std::filesystem::path(home ? home : ".");
}

}  // namespace kabot::session
