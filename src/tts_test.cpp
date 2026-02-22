#include "agent/tools/tts.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>

int main(int argc, char** argv) {
    std::string text = argc > 1 ? argv[1] : "测试你妹啊测试";
    std::string voice = argc > 2 ? argv[2] : "zh-CN-XiaoyiNeural";
    std::string audio_path = argc > 3 ? argv[3] : "";

    const auto workspace = std::filesystem::current_path().string();
    kabot::agent::tools::EdgeTtsTool tool(workspace);

    std::unordered_map<std::string, std::string> params;
    params["text"] = text;
    params["voice"] = voice;
    if (!audio_path.empty()) {
        params["audio_path"] = audio_path;
    }

    const auto result = tool.Execute(params);
    std::cout << result << std::endl;
    return 0;
}
