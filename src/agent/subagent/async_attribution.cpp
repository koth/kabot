#include "agent/subagent/async_attribution.hpp"

namespace kabot::subagent {

thread_local AttributionContext current_attribution;

void AsyncAttribution::Set(const AttributionContext& ctx) {
    current_attribution = ctx;
}

AttributionContext AsyncAttribution::Get() {
    return current_attribution;
}

void AsyncAttribution::Clear() {
    current_attribution = {};
}

} // namespace kabot::subagent
