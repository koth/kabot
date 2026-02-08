#include "agent/skills_loader.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include "nlohmann/json.hpp"

namespace kabot::agent {

SkillsLoader::SkillsLoader(std::string workspace)
    : workspace_(std::move(workspace))
    , skills_dir_((std::filesystem::path(workspace_) / "skills").string()) {
    auto candidate = std::filesystem::current_path() / "kabot" / "skills";
    if (std::filesystem::exists(candidate)) {
        builtin_skills_dir_ = candidate.string();
    } else {
        builtin_skills_dir_ = (std::filesystem::path(workspace_) / ".." / "kabot" / "skills").string();
    }
}

std::vector<SkillsLoader::SkillInfo> SkillsLoader::ListSkills(bool filter_unavailable) const {
    std::vector<SkillInfo> skills;
    std::filesystem::path workspace_path(skills_dir_);
    if (std::filesystem::exists(workspace_path)) {
        for (const auto& entry : std::filesystem::directory_iterator(workspace_path)) {
            if (!entry.is_directory()) {
                continue;
            }
            const auto skill_file = entry.path() / "SKILL.md";
            if (!std::filesystem::exists(skill_file)) {
                continue;
            }
            skills.push_back(SkillInfo{
                entry.path().filename().string(),
                skill_file.string(),
                "workspace"});
        }
    }

    std::filesystem::path builtin_path(builtin_skills_dir_);
    if (std::filesystem::exists(builtin_path)) {
        for (const auto& entry : std::filesystem::directory_iterator(builtin_path)) {
            if (!entry.is_directory()) {
                continue;
            }
            const auto name = entry.path().filename().string();
            const auto skill_file = entry.path() / "SKILL.md";
            if (!std::filesystem::exists(skill_file)) {
                continue;
            }
            const auto exists = std::any_of(skills.begin(), skills.end(), [&](const SkillInfo& info) {
                return info.name == name;
            });
            if (!exists) {
                skills.push_back(SkillInfo{name, skill_file.string(), "builtin"});
            }
        }
    }

    if (!filter_unavailable) {
        return skills;
    }
    std::vector<SkillInfo> filtered;
    filtered.reserve(skills.size());
    for (const auto& skill : skills) {
        if (CheckRequirements(GetSkillMeta(skill.name))) {
            filtered.push_back(skill);
        }
    }
    return filtered;
}

std::vector<std::string> SkillsLoader::LoadSkillNames() const {
    std::vector<std::string> skills;
    std::filesystem::path workspace_path(skills_dir_);
    if (std::filesystem::exists(workspace_path)) {
        for (const auto& entry : std::filesystem::directory_iterator(workspace_path)) {
            if (entry.is_directory()) {
                const auto skill_file = entry.path() / "SKILL.md";
                if (std::filesystem::exists(skill_file)) {
                    skills.push_back(entry.path().filename().string());
                }
            }
        }
    }

    std::filesystem::path builtin_path(builtin_skills_dir_);
    if (std::filesystem::exists(builtin_path)) {
        for (const auto& entry : std::filesystem::directory_iterator(builtin_path)) {
            if (entry.is_directory()) {
                const auto name = entry.path().filename().string();
                const auto skill_file = entry.path() / "SKILL.md";
                if (!std::filesystem::exists(skill_file)) {
                    continue;
                }
                if (std::find(skills.begin(), skills.end(), name) == skills.end()) {
                    skills.push_back(name);
                }
            }
        }
    }
    return skills;
}

std::string SkillsLoader::LoadSkill(const std::string& name) const {
    return LoadSkillContent(name);
}

std::string SkillsLoader::LoadSkillsForContext(const std::vector<std::string>& skill_names) const {
    std::ostringstream oss;
    bool has_content = false;
    for (const auto& name : skill_names) {
        auto content = LoadSkillContent(name);
        if (content.empty()) {
            continue;
        }
        content = StripFrontmatter(content);
        oss << "### Skill: " << name << "\n\n" << content << "\n\n";
        has_content = true;
    }
    return has_content ? oss.str() : std::string();
}

std::string SkillsLoader::BuildSkillsSummary() const {
    std::ostringstream oss;
    auto skills = ListSkills(false);
    if (skills.empty()) {
        return {};
    }
    auto escape_xml = [](const std::string& input) {
        std::string out;
        out.reserve(input.size());
        for (const auto ch : input) {
            switch (ch) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                default: out += ch; break;
            }
        }
        return out;
    };
    oss << "<skills>\n";
    for (const auto& skill : skills) {
        const auto meta = GetSkillMeta(skill.name);
        const auto available = CheckRequirements(meta);
        const auto description = meta.description.empty() ? skill.name : meta.description;
        oss << "  <skill available=\"" << (available ? "true" : "false") << "\">\n";
        oss << "    <name>" << escape_xml(skill.name) << "</name>\n";
        oss << "    <description>" << escape_xml(description) << "</description>\n";
        oss << "    <location>" << escape_xml(skill.path) << "</location>\n";
        if (!available) {
            const auto missing = MissingRequirements(meta);
            if (!missing.empty()) {
                oss << "    <requires>" << escape_xml(missing) << "</requires>\n";
            }
        }
        oss << "  </skill>\n";
    }
    oss << "</skills>";
    return oss.str();
}

std::vector<std::string> SkillsLoader::GetAlwaysSkills() const {
    std::vector<std::string> result;
    for (const auto& skill : ListSkills(true)) {
        const auto meta = GetSkillMeta(skill.name);
        if (meta.always && CheckRequirements(meta)) {
            result.push_back(skill.name);
        }
    }
    return result;
}

std::optional<std::unordered_map<std::string, std::string>> SkillsLoader::GetSkillMetadata(
    const std::string& name) const {
    const auto content = LoadSkillContent(name);
    if (content.empty()) {
        return std::nullopt;
    }
    if (content.rfind("---", 0) != 0) {
        return std::nullopt;
    }
    const auto end = content.find("---", 3);
    if (end == std::string::npos) {
        return std::nullopt;
    }
    return ParseFrontmatter(content);
}

std::string SkillsLoader::LoadSkillContent(const std::string& name) const {
    std::filesystem::path workspace_path = std::filesystem::path(skills_dir_) / name / "SKILL.md";
    std::filesystem::path builtin_path = std::filesystem::path(builtin_skills_dir_) / name / "SKILL.md";
    std::filesystem::path path;
    if (std::filesystem::exists(workspace_path)) {
        path = workspace_path;
    } else if (std::filesystem::exists(builtin_path)) {
        path = builtin_path;
    } else {
        return {};
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::unordered_map<std::string, std::string> SkillsLoader::ParseFrontmatter(
    const std::string& content) const {
    std::unordered_map<std::string, std::string> meta;
    if (content.rfind("---", 0) != 0) {
        return meta;
    }
    const auto end = content.find("---", 3);
    if (end == std::string::npos) {
        return meta;
    }
    std::istringstream stream(content.substr(3, end - 3));
    std::string line;
    while (std::getline(stream, line)) {
        auto pos = line.find(':');
        if (pos == std::string::npos) {
            continue;
        }
        auto key = line.substr(0, pos);
        auto value = line.substr(pos + 1);
        auto trim = [](std::string& text) {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
                text.erase(text.begin());
            }
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
                text.pop_back();
            }
        };
        trim(key);
        trim(value);
        if (!value.empty() && ((value.front() == '"' && value.back() == '"') ||
                               (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        if (!key.empty() && !value.empty()) {
            meta[key] = value;
        }
    }
    return meta;
}

std::string SkillsLoader::StripFrontmatter(const std::string& content) const {
    if (content.rfind("---", 0) != 0) {
        return content;
    }
    const auto end = content.find("---", 3);
    if (end == std::string::npos) {
        return content;
    }
    auto stripped = content.substr(end + 3);
    while (!stripped.empty() && std::isspace(static_cast<unsigned char>(stripped.front()))) {
        stripped.erase(stripped.begin());
    }
    while (!stripped.empty() && std::isspace(static_cast<unsigned char>(stripped.back()))) {
        stripped.pop_back();
    }
    return stripped;
}

SkillsLoader::SkillMeta SkillsLoader::GetSkillMeta(const std::string& name) const {
    SkillMeta meta;
    const auto content = LoadSkillContent(name);
    if (content.empty()) {
        return meta;
    }
    const auto front = ParseFrontmatter(content);
    auto it_desc = front.find("description");
    if (it_desc != front.end()) {
        meta.description = it_desc->second;
    }
    auto it_always = front.find("always");
    if (it_always != front.end()) {
        meta.always = (it_always->second == "true" || it_always->second == "True");
    }

    auto it_meta = front.find("metadata");
    if (it_meta != front.end()) {
        const auto parsed = ParseKabotMetadata(it_meta->second);
        if (!parsed.description.empty()) {
            meta.description = parsed.description;
        }
        if (parsed.always) {
            meta.always = true;
        }
        meta.bins.insert(meta.bins.end(), parsed.bins.begin(), parsed.bins.end());
        meta.envs.insert(meta.envs.end(), parsed.envs.begin(), parsed.envs.end());
    }

    return meta;
}

SkillsLoader::SkillMeta SkillsLoader::ParseKabotMetadata(const std::string& raw) const {
    SkillMeta meta;
    if (raw.empty()) {
        return meta;
    }
    std::string json = raw;
    if ((json.front() == '"' && json.back() == '"') ||
        (json.front() == '\'' && json.back() == '\'')) {
        json = json.substr(1, json.size() - 2);
    }

    try {
        auto parsed = nlohmann::json::parse(json);
        if (parsed.is_object() && parsed.contains("kabot")) {
            parsed = parsed["kabot"];
        }
        if (!parsed.is_object()) {
            return meta;
        }
        if (parsed.contains("description") && parsed["description"].is_string()) {
            meta.description = parsed["description"].get<std::string>();
        }
        if (parsed.contains("always") && parsed["always"].is_boolean()) {
            meta.always = parsed["always"].get<bool>();
        }
        if (parsed.contains("requires") && parsed["requires"].is_object()) {
            const auto& requires = parsed["requires"];
            if (requires.contains("bins") && requires["bins"].is_array()) {
                for (const auto& item : requires["bins"]) {
                    if (item.is_string()) {
                        meta.bins.push_back(item.get<std::string>());
                    }
                }
            }
            if (requires.contains("env") && requires["env"].is_array()) {
                for (const auto& item : requires["env"]) {
                    if (item.is_string()) {
                        meta.envs.push_back(item.get<std::string>());
                    }
                }
            }
        }
    } catch (...) {
        return meta;
    }

    return meta;
}

bool SkillsLoader::CheckRequirements(const SkillMeta& meta) const {
    for (const auto& bin : meta.bins) {
        if (!HasBinary(bin)) {
            return false;
        }
    }
    for (const auto& env : meta.envs) {
        if (!std::getenv(env.c_str())) {
            return false;
        }
    }
    return true;
}

std::string SkillsLoader::MissingRequirements(const SkillMeta& meta) const {
    std::vector<std::string> missing;
    for (const auto& bin : meta.bins) {
        if (!HasBinary(bin)) {
            missing.push_back("CLI: " + bin);
        }
    }
    for (const auto& env : meta.envs) {
        if (!std::getenv(env.c_str())) {
            missing.push_back("ENV: " + env);
        }
    }
    std::ostringstream oss;
    for (std::size_t i = 0; i < missing.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << missing[i];
    }
    return oss.str();
}

bool SkillsLoader::HasBinary(const std::string& name) const {
    const auto* path_env = std::getenv("PATH");
    if (!path_env) {
        return false;
    }
    std::string paths = path_env;
    std::regex sep_re("[;:]");
    std::sregex_token_iterator it(paths.begin(), paths.end(), sep_re, -1);
    std::sregex_token_iterator end;
    for (; it != end; ++it) {
        if (it->str().empty()) {
            continue;
        }
        std::filesystem::path base = it->str();
        std::filesystem::path candidate = base / name;
        if (std::filesystem::exists(candidate)) {
            return true;
        }
#if defined(_WIN32)
        std::filesystem::path exe = base / (name + ".exe");
        if (std::filesystem::exists(exe)) {
            return true;
        }
#endif
    }
    return false;
}

}  // namespace kabot::agent
