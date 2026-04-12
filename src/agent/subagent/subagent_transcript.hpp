#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "agent/subagent/subagent_types.hpp"
#include "providers/llm_provider.hpp"

namespace kabot::subagent {

class SubagentTranscriptStore {
public:
    explicit SubagentTranscriptStore(std::string workspace);
    
    void WriteMetadata(const AgentTranscriptMetadata& metadata);
    void AppendMessages(const std::string& agent_id,
                        const std::vector<kabot::providers::Message>& messages);
    void AppendMessage(const std::string& agent_id,
                       const kabot::providers::Message& message);
    
    AgentTranscriptMetadata LoadMetadata(const std::string& agent_id) const;
    std::vector<kabot::providers::Message> LoadMessages(const std::string& agent_id) const;
    
    std::filesystem::path MetadataPath(const std::string& agent_id) const;
    std::filesystem::path TranscriptPath(const std::string& agent_id) const;
    
private:
    std::string workspace_;
};

} // namespace kabot::subagent
