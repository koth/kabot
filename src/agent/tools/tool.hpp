#pragma once

#include <string>
#include <unordered_map>

namespace kabot::agent::tools {

class Tool {
public:
    virtual ~Tool() = default;
    virtual std::string Name() const = 0;
    virtual std::string Description() const = 0;
    virtual std::string ParametersJson() const = 0;
    virtual std::string Execute(const std::unordered_map<std::string, std::string>& params) = 0;

    // Phase-2 architecture metadata
    virtual bool IsReadOnly() const { return false; }
    virtual bool IsDestructive() const { return false; }
    virtual bool IsConcurrencySafe() const { return false; }
};

}  // namespace kabot::agent::tools
