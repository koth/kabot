#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

namespace kabot::agent {

class SkillsLoader {
public:
    explicit SkillsLoader(std::string workspace);
    struct SkillInfo {
        std::string name;
        std::string path;
        std::string source;
    };

    std::vector<SkillInfo> ListSkills(bool filter_unavailable = true) const;
    std::string LoadSkill(const std::string& name) const;
    std::vector<std::string> LoadSkillNames() const;
    std::string LoadSkillsForContext(const std::vector<std::string>& skill_names) const;
    std::string BuildSkillsSummary() const;
    std::vector<std::string> GetAlwaysSkills() const;
    std::optional<std::unordered_map<std::string, std::string>> GetSkillMetadata(
        const std::string& name) const;

private:
    std::string workspace_;
    std::string skills_dir_;
    std::string builtin_skills_dir_;

    std::string LoadSkillContent(const std::string& name) const;
    std::unordered_map<std::string, std::string> ParseFrontmatter(const std::string& content) const;
    std::string StripFrontmatter(const std::string& content) const;

    struct SkillMeta {
        std::string description;
        bool always = false;
        std::vector<std::string> bins;
        std::vector<std::string> envs;
    };

    SkillMeta GetSkillMeta(const std::string& name) const;
    SkillMeta ParseKabotMetadata(const std::string& raw) const;
    bool CheckRequirements(const SkillMeta& meta) const;
    std::string MissingRequirements(const SkillMeta& meta) const;
    bool HasBinary(const std::string& name) const;
};

}  // namespace kabot::agent
