#include "agent/tools/tool_schema_validator.hpp"

#include "agent/tools/tool.hpp"
#include "nlohmann/json.hpp"
#include "utils/logging.hpp"

namespace kabot::agent::tools {
namespace {

bool IsBooleanString(const std::string& value) {
    return value == "true" || value == "false";
}

bool IsIntegerString(const std::string& value) {
    if (value.empty()) return false;
    try {
        size_t pos = 0;
        (void)std::stoll(value, &pos);
        return pos == value.size();
    } catch (...) {
        return false;
    }
}

bool IsNumberString(const std::string& value) {
    if (value.empty()) return false;
    try {
        size_t pos = 0;
        (void)std::stod(value, &pos);
        return pos == value.size();
    } catch (...) {
        return false;
    }
}

bool IsArrayString(const std::string& value) {
    try {
        auto j = nlohmann::json::parse(value);
        return j.is_array();
    } catch (...) {
        return false;
    }
}

bool IsObjectString(const std::string& value) {
    try {
        auto j = nlohmann::json::parse(value);
        return j.is_object();
    } catch (...) {
        return false;
    }
}

bool IsEnumValid(const nlohmann::json& enum_values, const std::string& value) {
    if (!enum_values.is_array()) return true;
    for (const auto& ev : enum_values) {
        if (ev.is_string() && ev.get<std::string>() == value) return true;
        if (ev.is_boolean() && ((ev.get<bool>() && value == "true") || (!ev.get<bool>() && value == "false"))) return true;
    }
    return false;
}

}  // namespace

std::string ValidateToolInput(const Tool* tool,
                               const std::unordered_map<std::string, std::string>& params) {
    if (!tool) {
        return "";
    }

    nlohmann::json schema;
    try {
        schema = nlohmann::json::parse(tool->ParametersJson());
    } catch (const std::exception& ex) {
        LOG_WARN("[tool] failed to parse parameters json for tool={} error={}", tool->Name(), ex.what());
        return "";
    }

    // Check required fields
    if (schema.contains("required") && schema["required"].is_array()) {
        for (const auto& req : schema["required"]) {
            if (!req.is_string()) continue;
            auto key = req.get<std::string>();
            auto it = params.find(key);
            if (it == params.end() || it->second.empty()) {
                return "Error: missing required parameter '" + key + "' for tool '" + tool->Name() + "'";
            }
        }
    }

    // Validate individual fields if properties are defined
    if (schema.contains("properties") && schema["properties"].is_object()) {
        for (const auto& [key, value] : params) {
            if (value.empty()) continue;  // skip empty for validation (required already handled it)
            auto prop_it = schema["properties"].find(key);
            if (prop_it == schema["properties"].end()) {
                // Unknown parameter: strict mode could reject, but we'll allow for now
                continue;
            }
            const auto& prop = *prop_it;
            if (!prop.is_object()) continue;

            auto type_it = prop.find("type");
            if (type_it == prop.end()) continue;
            std::string type = type_it->is_string() ? type_it->get<std::string>() : "";

            bool valid = false;
            if (type == "string") {
                valid = true;  // Any non-empty string is valid string in this flat representation
            } else if (type == "boolean") {
                valid = IsBooleanString(value);
            } else if (type == "integer") {
                valid = IsIntegerString(value);
            } else if (type == "number") {
                valid = IsNumberString(value);
            } else if (type == "array") {
                valid = IsArrayString(value);
            } else if (type == "object") {
                valid = IsObjectString(value);
            } else {
                valid = true;
            }

            if (!valid) {
                return "Error: parameter '" + key + "' for tool '" + tool->Name() +
                       "' expected type '" + type + "' but got value '" + value + "'";
            }

            auto enum_it = prop.find("enum");
            if (enum_it != prop.end()) {
                if (!IsEnumValid(*enum_it, value)) {
                    return "Error: parameter '" + key + "' for tool '" + tool->Name() +
                           "' has invalid enum value '" + value + "'";
                }
            }
        }
    }

    return "";
}

}  // namespace kabot::agent::tools
