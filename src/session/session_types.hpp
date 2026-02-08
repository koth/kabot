#pragma once

#include <string>
#include <unordered_map>

namespace kabot::session {

struct SessionKey {
    std::string channel;
    std::string chat_id;

    std::string ToString() const {
        return channel + ":" + chat_id;
    }
};

struct SessionState {
    std::unordered_map<std::string, std::string> attributes;
};

}  // namespace kabot::session
