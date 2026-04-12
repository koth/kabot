#include "agent/subagent/subagent_transcript.hpp"

#include <filesystem>
#include <fstream>

#include "nlohmann/json.hpp"
#include "utils/logging.hpp"

namespace kabot::subagent {

SubagentTranscriptStore::SubagentTranscriptStore(std::string workspace)
    : workspace_(std::move(workspace)) {}

std::filesystem::path SubagentTranscriptStore::MetadataPath(const std::string& agent_id) const {
    return std::filesystem::path(workspace_) / "subagents" / (agent_id + "_meta.json");
}

std::filesystem::path SubagentTranscriptStore::TranscriptPath(const std::string& agent_id) const {
    return std::filesystem::path(workspace_) / "subagents" / (agent_id + "_transcript.json");
}

void SubagentTranscriptStore::WriteMetadata(const AgentTranscriptMetadata& metadata) {
    auto path = MetadataPath(metadata.agent_id);
    std::filesystem::create_directories(path.parent_path());
    
    nlohmann::json j;
    j["agentId"] = metadata.agent_id;
    j["agentType"] = metadata.agent_type;
    j["description"] = metadata.description;
    j["worktreePath"] = metadata.worktree_path;
    j["parentAgentId"] = metadata.parent_agent_id;
    j["parentSessionId"] = metadata.parent_session_id;
    j["invocationKind"] = metadata.invocation_kind;
    j["createdAt"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        metadata.created_at.time_since_epoch()).count();
    
    std::ofstream out(path, std::ios::trunc);
    out << j.dump(2);
}

void SubagentTranscriptStore::AppendMessage(const std::string& agent_id,
                                            const kabot::providers::Message& message) {
    AppendMessages(agent_id, {message});
}

void SubagentTranscriptStore::AppendMessages(const std::string& agent_id,
                                             const std::vector<kabot::providers::Message>& messages) {
    auto path = TranscriptPath(agent_id);
    std::filesystem::create_directories(path.parent_path());
    
    nlohmann::json j = nlohmann::json::array();
    if (std::filesystem::exists(path)) {
        std::ifstream in(path);
        if (in.is_open()) {
            try {
                in >> j;
            } catch (...) {
                j = nlohmann::json::array();
            }
        }
    }
    
    for (const auto& msg : messages) {
        nlohmann::json entry;
        entry["role"] = msg.role;
        entry["content"] = msg.content;
        entry["name"] = msg.name;
        entry["toolCallId"] = msg.tool_call_id;
        if (!msg.tool_calls.empty()) {
            entry["toolCalls"] = nlohmann::json::array();
            for (const auto& tc : msg.tool_calls) {
                nlohmann::json tcj;
                tcj["id"] = tc.id;
                tcj["name"] = tc.name;
                if (!tc.arguments.empty()) {
                    nlohmann::json args = nlohmann::json::object();
                    for (const auto& [k, v] : tc.arguments) {
                        args[k] = v;
                    }
                    tcj["arguments"] = args;
                }
                entry["toolCalls"].push_back(tcj);
            }
        }
        j.push_back(entry);
    }
    
    std::ofstream out(path, std::ios::trunc);
    out << j.dump(2);
}

AgentTranscriptMetadata SubagentTranscriptStore::LoadMetadata(const std::string& agent_id) const {
    AgentTranscriptMetadata meta;
    auto path = MetadataPath(agent_id);
    if (!std::filesystem::exists(path)) {
        return meta;
    }
    std::ifstream in(path);
    nlohmann::json j;
    try {
        in >> j;
    } catch (...) {
        return meta;
    }
    
    meta.agent_id = j.value("agentId", "");
    meta.agent_type = j.value("agentType", "");
    meta.description = j.value("description", "");
    meta.worktree_path = j.value("worktreePath", "");
    meta.parent_agent_id = j.value("parentAgentId", "");
    meta.parent_session_id = j.value("parentSessionId", "");
    meta.invocation_kind = j.value("invocationKind", "spawn");
    auto created_ms = j.value("createdAt", 0LL);
    meta.created_at = std::chrono::steady_clock::time_point(
        std::chrono::milliseconds(created_ms));
    return meta;
}

std::vector<kabot::providers::Message> SubagentTranscriptStore::LoadMessages(const std::string& agent_id) const {
    std::vector<kabot::providers::Message> result;
    auto path = TranscriptPath(agent_id);
    if (!std::filesystem::exists(path)) {
        return result;
    }
    std::ifstream in(path);
    nlohmann::json j;
    try {
        in >> j;
    } catch (...) {
        return result;
    }
    
    if (!j.is_array()) return result;
    for (const auto& entry : j) {
        kabot::providers::Message msg;
        msg.role = entry.value("role", "");
        msg.content = entry.value("content", "");
        msg.name = entry.value("name", "");
        msg.tool_call_id = entry.value("toolCallId", "");
        if (entry.contains("toolCalls") && entry["toolCalls"].is_array()) {
            for (const auto& tcj : entry["toolCalls"]) {
                kabot::providers::ToolCallRequest tc;
                tc.id = tcj.value("id", "");
                tc.name = tcj.value("name", "");
                if (tcj.contains("arguments") && tcj["arguments"].is_object()) {
                    for (auto& [k, v] : tcj["arguments"].items()) {
                        tc.arguments[k] = v.get<std::string>();
                    }
                }
                msg.tool_calls.push_back(tc);
            }
        }
        result.push_back(msg);
    }
    return result;
}

} // namespace kabot::subagent
