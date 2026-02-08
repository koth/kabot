#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <sstream>

namespace kabot::utils {

using String = std::string;

inline std::string Join(const std::vector<std::string>& items, const std::string& delimiter) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            oss << delimiter;
        }
        oss << items[i];
    }
    return oss.str();
}

inline std::chrono::system_clock::time_point Now() {
    return std::chrono::system_clock::now();
}

}  // namespace kabot::utils
