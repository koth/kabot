#include "agent/tools/filesystem.hpp"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace kabot::agent::tools {
namespace {

std::string GetParam(const std::unordered_map<std::string, std::string>& params,
                     const std::string& name) {
    auto it = params.find(name);
    if (it == params.end()) {
        return {};
    }
    return it->second;
}

}  // namespace

std::string ReadFileTool::ParametersJson() const {
    return R"json({"type":"object","properties":{"path":{"type":"string","description":"File path to read"},"line":{"type":"integer","minimum":1,"description":"Start line number (1-based)"},"limit":{"type":"integer","minimum":1,"description":"Maximum number of lines to read"}},"required":["path"]})json";
}

namespace {

int GetParamInt(const std::unordered_map<std::string, std::string>& params,
                const std::string& name,
                int default_value) {
    auto it = params.find(name);
    if (it == params.end()) {
        return default_value;
    }
    try {
        return std::stoi(it->second);
    } catch (...) {
        return default_value;
    }
}

}  // namespace

std::string ReadFileTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    const auto path = GetParam(params, "path");
    if (path.empty()) {
        return "Error: path is required";
    }
    const int start_line = GetParamInt(params, "line", 1);
    const int max_lines = GetParamInt(params, "limit", -1);

    std::ifstream file(path, std::ios::in);
    if (!file.is_open()) {
        return "Error: failed to open file";
    }

    std::string line;
    int line_num = 0;
    int lines_read = 0;
    std::ostringstream oss;

    while (std::getline(file, line)) {
        ++line_num;
        if (line_num < start_line) {
            continue;
        }
        if (max_lines >= 0 && lines_read >= max_lines) {
            int remaining = 0;
            while (std::getline(file, line)) {
                ++remaining;
            }
            oss << "\n... (truncated after " << max_lines << " lines; total " << (line_num + remaining) << " lines) ...";
            break;
        }
        if (lines_read > 0) {
            oss << '\n';
        }
        oss << line;
        ++lines_read;
    }

    if (lines_read == 0) {
        return "[Empty file or start line beyond end of file]";
    }
    return oss.str();
}

std::string WriteFileTool::ParametersJson() const {
    return R"({"type":"object","properties":{"path":{"type":"string","description":"File path to write"},"content":{"type":"string","description":"Content to write"}},"required":["path","content"]})";
}

std::string WriteFileTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    const auto path = GetParam(params, "path");
    const auto content = GetParam(params, "content");
    if (path.empty()) {
        return "Error: path is required";
    }
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return "Error: failed to open file";
    }
    file << content;
    return "OK";
}

std::string EditFileTool::ParametersJson() const {
    return R"({"type":"object","properties":{"path":{"type":"string","description":"File path to edit"},"old_string":{"type":"string","description":"Exact existing text to replace"},"new_string":{"type":"string","description":"Replacement text"}},"required":["path","old_string","new_string"]})";
}

std::string EditFileTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    const auto path = GetParam(params, "path");
    const auto old_str = GetParam(params, "old_string");
    const auto new_str = GetParam(params, "new_string");
    if (path.empty()) {
        return "Error: path is required";
    }
    std::ifstream in(path, std::ios::in);
    if (!in.is_open()) {
        return "Error: failed to open file";
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    auto content = buffer.str();
    in.close();

    auto pos = content.find(old_str);
    if (pos == std::string::npos) {
        return "Error: old_string not found";
    }
    content.replace(pos, old_str.size(), new_str);

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return "Error: failed to write file";
    }
    out << content;
    return "OK";
}

std::string ListDirTool::ParametersJson() const {
    return R"({"type":"object","properties":{"path":{"type":"string","description":"Directory path to list"}},"required":["path"]})";
}

std::string ListDirTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    const auto path = GetParam(params, "path");
    if (path.empty()) {
        return "Error: path is required";
    }
    try {
        std::filesystem::path root(path);
        if (!std::filesystem::exists(root)) {
            return "Error: path does not exist";
        }
        if (!std::filesystem::is_directory(root)) {
            return "Error: path is not a directory";
        }
        std::ostringstream oss;
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            oss << entry.path().filename().string() << "\n";
        }
        return oss.str();
    } catch (const std::filesystem::filesystem_error& e) {
        return std::string("Error: ") + e.what();
    } catch (const std::exception& e) {
        return std::string("Error: ") + e.what();
    }
}

std::string GlobTool::ParametersJson() const {
    return R"json({"type":"object","properties":{"pattern":{"type":"string","description":"Glob pattern such as **/*.cpp or src/*.hpp"},"path":{"type":"string","description":"Base directory (default: current directory)"}},"required":["pattern"]})json";
}

std::string GlobTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    const auto pattern = GetParam(params, "pattern");
    const auto base = GetParam(params, "path");
    if (pattern.empty()) {
        return "Error: pattern is required";
    }
    std::filesystem::path root = base.empty() ? std::filesystem::current_path() : std::filesystem::path(base);
    if (!std::filesystem::exists(root)) {
        return "Error: path does not exist";
    }

    // Convert glob to regex
    std::string re_str;
    for (char c : pattern) {
        if (c == '*') {
            re_str += ".*";
        } else if (c == '?') {
            re_str += ".";
        } else if (c == '.') {
            re_str += "\\.";
        } else if (c == '/' || c == '\\') {
            re_str += "[/\\\\]";
        } else {
            re_str += c;
        }
    }
    std::regex re(re_str);

    std::ostringstream oss;
    int count = 0;
    const auto root_str = root.string();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        auto rel = std::filesystem::relative(entry.path(), root).string();
        // Normalize separators for matching
        std::replace(rel.begin(), rel.end(), '\\', '/');
        if (std::regex_match(rel, re)) {
            oss << rel << "\n";
            ++count;
            if (count >= 100) {
                oss << "... (truncated at 100 results)\n";
                break;
            }
        }
    }
    if (count == 0) {
        return "No files matched.";
    }
    return oss.str();
}

std::string GrepTool::ParametersJson() const {
    return R"({"type":"object","properties":{"pattern":{"type":"string","description":"Regex pattern to search"},"path":{"type":"string","description":"File or directory to search"}},"required":["pattern","path"]})";
}

std::string GrepTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    const auto pattern = GetParam(params, "pattern");
    const auto path_str = GetParam(params, "path");
    if (pattern.empty() || path_str.empty()) {
        return "Error: pattern and path are required";
    }
    try {
        std::regex re(pattern);
        std::ostringstream oss;
        int matches = 0;
        std::filesystem::path target(path_str);

        auto search_file = [&](const std::filesystem::path& p) {
            std::ifstream file(p, std::ios::in);
            if (!file.is_open()) return;
            std::string line;
            int line_no = 0;
            while (std::getline(file, line)) {
                ++line_no;
                if (std::regex_search(line, re)) {
                    oss << std::filesystem::relative(p, std::filesystem::current_path()).string()
                       << ":" << line_no << ": " << line << "\n";
                    ++matches;
                    if (matches >= 50) {
                        oss << "... (truncated at 50 matches)\n";
                        return;
                    }
                }
            }
        };

        if (std::filesystem::is_regular_file(target)) {
            search_file(target);
        } else if (std::filesystem::is_directory(target)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(target)) {
                if (!entry.is_regular_file()) continue;
                search_file(entry.path());
                if (matches >= 50) break;
            }
        } else {
            return "Error: path is not a file or directory";
        }

        if (matches == 0) {
            return "No matches found.";
        }
        return oss.str();
    } catch (const std::regex_error& ex) {
        return std::string("Error: invalid regex: ") + ex.what();
    }
}

} // namespace kabot::agent::tools
