#pragma once

#include <string>

namespace kabot::subagent {

struct AttributionContext {
    std::string agent_id;
    std::string parent_session_id;
    std::string agent_type;
    std::string subagent_name;
    bool is_built_in = false;
    std::string invoking_request_id;
    std::string invocation_kind;
};

class AsyncAttribution {
public:
    static void Set(const AttributionContext& ctx);
    static AttributionContext Get();
    static void Clear();
};

} // namespace kabot::subagent
