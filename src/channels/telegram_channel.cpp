#include "channels/telegram_channel.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <regex>
#include <vector>
#include <tgbot/net/CurlHttpClient.h>

namespace kabot::channels {

TelegramChannel::TelegramChannel(const kabot::config::TelegramConfig& config,
                                 kabot::bus::MessageBus& bus)
    : ChannelBase("telegram", bus, config.allow_from)
    , config_(config) {}

std::string TelegramChannel::JoinParts(const std::vector<std::string>& parts) const {
    if (parts.empty()) {
        return "[empty message]";
    }
    std::string joined;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            joined += "\n";
        }
        joined += parts[i];
    }
    return joined;
}

std::string TelegramChannel::ResolveMediaPath(const std::string& media_id,
                                              const std::string& ext) const {
    if (media_id.empty()) {
        return {};
    }
    const char* home = std::getenv("HOME");
#if defined(_WIN32)
    if (!home) {
        home = std::getenv("USERPROFILE");
    }
#endif
    std::filesystem::path base = std::filesystem::path(home ? home : ".");
    base /= ".kabot";
    base /= "media";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    const auto filename = media_id.substr(0, 16) + ext;
    return (base / filename).string();
}

void TelegramChannel::Start() {
    if (running_) {
        return;
    }
    if (config_.token.empty()) {
        std::cerr << "[telegram] token is empty; channel disabled" << std::endl;
        running_ = false;
        return;
    }
    running_ = true;
    polling_ = true;

    const bool use_curl = std::getenv("KABOT_TELEGRAM_USE_CURL") != nullptr ||
                          std::getenv("HTTPS_PROXY") != nullptr ||
                          std::getenv("HTTP_PROXY") != nullptr ||
                          std::getenv("ALL_PROXY") != nullptr ||
                          std::getenv("https_proxy") != nullptr ||
                          std::getenv("http_proxy") != nullptr ||
                          std::getenv("all_proxy") != nullptr;
    if (use_curl) {
#ifdef HAVE_CURL
        http_client_ = std::make_unique<TgBot::CurlHttpClient>();
        bot_ = std::make_unique<TgBot::Bot>(config_.token, *http_client_);
        std::cerr << "[telegram] bot initialized with CurlHttpClient (proxy-aware)" << std::endl;
#else
        bot_ = std::make_unique<TgBot::Bot>(config_.token);
        std::cerr << "[telegram] curl not available, fallback to BoostHttpOnlySslClient" << std::endl;
#endif
    } else {
        bot_ = std::make_unique<TgBot::Bot>(config_.token);
        std::cerr << "[telegram] bot initialized with BoostHttpOnlySslClient" << std::endl;
    }
    bot_->getEvents().onCommand("start", [this](TgBot::Message::Ptr message) {
        if (!message || !message->from) {
            return;
        }
        bot_->getApi().sendMessage(message->chat->id,
                                   "Hi! I'm kabot. Send me a message and I'll respond!");
    });

    bot_->getEvents().onAnyMessage([this](TgBot::Message::Ptr message) {
        if (!message || !message->from || !message->chat) {
            return;
        }
        if (message->text.find("/start") == 0) {
            return;
        }

        std::string sender_id = std::to_string(message->from->id);
        if (!message->from->username.empty()) {
            sender_id += "|" + message->from->username;
        }
        const std::string chat_id = std::to_string(message->chat->id);
        std::cerr << "[telegram] received message from " << sender_id
                  << " in chat " << chat_id << std::endl;
        std::string media_type;
        std::string mime_type;
        std::string media_id;
        std::string text = message->text;
        std::string caption = message->caption;

        if (!message->photo.empty()) {
            media_type = "image";
            auto photo = message->photo.back();
            media_id = photo->fileId;
        } else if (message->voice) {
            media_type = "voice";
            media_id = message->voice->fileId;
            mime_type = message->voice->mimeType;
        } else if (message->audio) {
            media_type = "audio";
            media_id = message->audio->fileId;
            mime_type = message->audio->mimeType;
        } else if (message->document) {
            media_type = "file";
            media_id = message->document->fileId;
            mime_type = message->document->mimeType;
        }

        std::unordered_map<std::string, std::string> metadata;
        metadata["message_id"] = std::to_string(message->messageId);
        metadata["user_id"] = std::to_string(message->from->id);
        metadata["username"] = message->from->username;
        metadata["first_name"] = message->from->firstName;
        metadata["is_group"] = message->chat->type != TgBot::Chat::Type::Private ? "true" : "false";

        if (!media_id.empty()) {
            const auto ext = GetMediaExtension(media_type, mime_type);
            const auto path = ResolveMediaPath(media_id, ext);
            try {
                auto file = bot_->getApi().getFile(media_id);
                const auto content = bot_->getApi().downloadFile(file->filePath);
                std::ofstream output(path, std::ios::binary);
                output.write(content.data(), static_cast<std::streamsize>(content.size()));
            } catch (const TgBot::TgException&) {
                metadata["media_download_error"] = "true";
            } catch (const std::exception&) {
                metadata["media_download_error"] = "true";
            }
        }

        if (message->messageId > 0) {
            message_chat_ids_[std::to_string(message->messageId)] = chat_id;
        }

        HandleIncomingMessage(sender_id, chat_id, text, caption, media_type, mime_type, media_id, metadata);
    });

    polling_thread_ = std::make_unique<std::thread>([this]() {
        long_poll_ = std::make_unique<TgBot::TgLongPoll>(*bot_);
        while (running_ && polling_) {
            try {
                long_poll_->start();
            } catch (const TgBot::TgException& ex) {
                std::cerr << "[telegram] long poll error: " << ex.what() << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            } catch (const std::exception& ex) {
                std::cerr << "[telegram] long poll error: " << ex.what() << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    });
}

void TelegramChannel::Stop() {
    running_ = false;
    polling_ = false;
    if (long_poll_) {
        try {
        } catch (const TgBot::TgException&) {
        } catch (const std::exception&) {
        }
    }
    if (polling_thread_ && polling_thread_->joinable()) {
        polling_thread_->join();
    }
    long_poll_.reset();
    bot_.reset();
    http_client_.reset();
}

void TelegramChannel::Send(const kabot::bus::OutboundMessage& msg) {
    if (!bot_) {
        return;
    }
    auto it_action = msg.metadata.find("action");
    if (it_action != msg.metadata.end() && it_action->second == "typing") {
        if (!msg.chat_id.empty()) {
            try {
                bot_->getApi().sendChatAction(std::stoll(msg.chat_id), "typing");
            } catch (const TgBot::TgException&) {
            } catch (const std::exception&) {
            }
        }
        return;
    }
    std::string chat_id = msg.chat_id;
    if (chat_id.empty() && !msg.reply_to.empty()) {
        auto it = message_chat_ids_.find(msg.reply_to);
        if (it != message_chat_ids_.end()) {
            chat_id = it->second;
        }
    }
    if (chat_id.empty()) {
        return;
    }
    long long reply_to_message_id = 0;
    if (!msg.reply_to.empty()) {
        try {
            reply_to_message_id = std::stoll(msg.reply_to);
        } catch (...) {
            reply_to_message_id = 0;
        }
    }
    const auto html = ConvertMarkdownToHtml(msg.content);
    try {
        bot_->getApi().sendMessage(std::stoll(chat_id), html, false, reply_to_message_id, nullptr, "HTML");
    } catch (const TgBot::TgException&) {
        bot_->getApi().sendMessage(std::stoll(chat_id), msg.content);
    } catch (const std::exception&) {
        bot_->getApi().sendMessage(std::stoll(chat_id), msg.content);
    }
}

void TelegramChannel::HandleIncomingMessage(
    const std::string& sender_id,
    const std::string& chat_id,
    const std::string& text,
    const std::string& caption,
    const std::string& media_type,
    const std::string& mime_type,
    const std::string& media_id,
    std::unordered_map<std::string, std::string> extra_metadata) {
    chat_ids_[sender_id] = chat_id;
    std::vector<std::string> parts;
    if (!text.empty()) {
        parts.push_back(text);
    }
    if (!caption.empty()) {
        parts.push_back(caption);
    }

    std::vector<std::string> media_paths;
    bool download_failed = false;
    auto it_error = extra_metadata.find("media_download_error");
    if (it_error != extra_metadata.end() && it_error->second == "true") {
        download_failed = true;
    }
    if (!media_id.empty()) {
        const auto ext = GetMediaExtension(media_type, mime_type);
        const auto path = ResolveMediaPath(media_id, ext);
        if (!path.empty() && !download_failed) {
            media_paths.push_back(path);
            parts.push_back("[" + media_type + ": " + path + "]");
        } else if (!media_type.empty()) {
            parts.push_back("[" + media_type + ": download failed]");
            download_failed = true;
        }
    }

    const auto content = JoinParts(parts);
    std::unordered_map<std::string, std::string> metadata = std::move(extra_metadata);
    if (!media_id.empty()) {
        metadata["media_id"] = media_id;
    }
    if (!media_type.empty()) {
        metadata["media_type"] = media_type;
    }
    if (download_failed) {
        metadata["media_download_error"] = "true";
    }
    metadata["chat_id"] = chat_id;
    HandleMessage(sender_id, chat_id, content, media_paths, metadata);
}

std::string TelegramChannel::ConvertMarkdownToHtml(const std::string& text) const {
    if (text.empty()) {
        return {};
    }
    std::string result = text;

    std::vector<std::string> code_blocks;
    std::vector<std::string> inline_codes;
    std::vector<std::string> tables;

    auto replace_and_store = [](const std::string& input,
                                const std::regex& pattern,
                                std::vector<std::string>& store,
                                const std::string& token_prefix) {
        std::string output;
        std::smatch match;
        std::string::const_iterator search_start = input.begin();
        while (std::regex_search(search_start, input.end(), match, pattern)) {
            output.append(search_start, match[0].first);
            store.push_back(match[1].str());
            const auto token = "[[KABOT_" + token_prefix + "_" + std::to_string(store.size() - 1) + "]]";
            output.append(token);
            search_start = match[0].second;
        }
        output.append(search_start, input.end());
        return output;
    };

    auto replace_all = [](std::string& input, const std::string& from, const std::string& to) {
        if (from.empty()) {
            return;
        }
        std::size_t start = 0;
        while ((start = input.find(from, start)) != std::string::npos) {
            input.replace(start, from.size(), to);
            start += to.size();
        }
    };

    auto replace_tables = [&](const std::string& input,
                              std::vector<std::string>& store) {
        auto count_pipes = [](const std::string& line) {
            return std::count(line.begin(), line.end(), '|');
        };
        auto is_table_row = [&](const std::string& line) {
            return count_pipes(line) >= 2;
        };
        auto is_separator = [](const std::string& line) {
            bool has_pipe = false;
            for (const auto ch : line) {
                if (ch == '|') {
                    has_pipe = true;
                    continue;
                }
                if (ch == '-' || ch == ':' || ch == ' ' || ch == '\t') {
                    continue;
                }
                return false;
            }
            return has_pipe;
        };

        std::vector<std::string> lines;
        std::string current;
        for (const auto ch : input) {
            if (ch == '\n') {
                lines.push_back(current);
                current.clear();
            } else {
                current.push_back(ch);
            }
        }
        lines.push_back(current);

        std::vector<std::string> output_lines;
        for (std::size_t i = 0; i < lines.size();) {
            if (i + 1 < lines.size() && is_table_row(lines[i]) && is_separator(lines[i + 1])) {
                std::string table_block = lines[i];
                std::size_t j = i + 1;
                while (j < lines.size() && is_table_row(lines[j])) {
                    table_block.append("\n");
                    table_block.append(lines[j]);
                    ++j;
                }
                store.push_back(table_block);
                output_lines.push_back("[[KABOT_TB_" + std::to_string(store.size() - 1) + "]]");
                i = j;
                continue;
            }
            output_lines.push_back(lines[i]);
            ++i;
        }

        std::string output;
        for (std::size_t i = 0; i < output_lines.size(); ++i) {
            if (i > 0) {
                output.append("\n");
            }
            output.append(output_lines[i]);
        }
        return output;
    };

    auto escape_html = [](const std::string& input) {
        std::string output = input;
        output = std::regex_replace(output, std::regex("&"), "&amp;");
        output = std::regex_replace(output, std::regex("<"), "&lt;");
        output = std::regex_replace(output, std::regex(">"), "&gt;");
        return output;
    };

    result = replace_and_store(result,
                               std::regex(R"(```[\w]*\n?([\s\S]*?)```)") ,
                               code_blocks,
                               "CB");
    result = replace_and_store(result, std::regex(R"(`([^`]+)`)") , inline_codes, "IC");
    result = replace_tables(result, tables);

    result = std::regex_replace(result, std::regex(R"((^|\n)#{1,6}\s+([^\n]+))"), "$1$2");
    result = std::regex_replace(result, std::regex(R"((^|\n)>\s*([^\n]*))"), "$1$2");

    result = std::regex_replace(result, std::regex("&"), "&amp;");
    result = std::regex_replace(result, std::regex("<"), "&lt;");
    result = std::regex_replace(result, std::regex(">"), "&gt;");

    result = std::regex_replace(result, std::regex(R"(\[([^\]]+)\]\(([^)]+)\))"), "<a href=\"$2\">$1</a>");
    result = std::regex_replace(result, std::regex(R"(\*\*(.+?)\*\*)"), "<b>$1</b>");
    result = std::regex_replace(result, std::regex(R"(__(.+?)__)"), "<b>$1</b>");
    result = std::regex_replace(result, std::regex(R"((^|[^a-zA-Z0-9])_([^_]+)_(?![a-zA-Z0-9]))"), "$1<i>$2</i>");
    result = std::regex_replace(result, std::regex(R"(~~(.+?)~~)"), "<s>$1</s>");
    result = std::regex_replace(result, std::regex(R"((^|\n)[-*]\s+)"), "$1• ");

    for (std::size_t i = 0; i < inline_codes.size(); ++i) {
        auto escaped = inline_codes[i];
        escaped = std::regex_replace(escaped, std::regex("&"), "&amp;");
        escaped = std::regex_replace(escaped, std::regex("<"), "&lt;");
        escaped = std::regex_replace(escaped, std::regex(">"), "&gt;");
        const auto token = "[[KABOT_IC_" + std::to_string(i) + "]]";
        replace_all(result, token, "<code>" + escaped + "</code>");
    }

    for (std::size_t i = 0; i < code_blocks.size(); ++i) {
        auto escaped = code_blocks[i];
        escaped = std::regex_replace(escaped, std::regex("&"), "&amp;");
        escaped = std::regex_replace(escaped, std::regex("<"), "&lt;");
        escaped = std::regex_replace(escaped, std::regex(">"), "&gt;");
        const auto token = "[[KABOT_CB_" + std::to_string(i) + "]]";
        replace_all(result, token, "<pre><code>" + escaped + "</code></pre>");
    }

    for (std::size_t i = 0; i < tables.size(); ++i) {
        const auto token = "[[KABOT_TB_" + std::to_string(i) + "]]";
        replace_all(result, token, "<pre>" + escape_html(tables[i]) + "</pre>");
    }

    return result;
}

std::string TelegramChannel::GetMediaExtension(const std::string& media_type,
                                               const std::string& mime_type) const {
    if (mime_type == "image/jpeg") return ".jpg";
    if (mime_type == "image/png") return ".png";
    if (mime_type == "image/gif") return ".gif";
    if (mime_type == "audio/ogg") return ".ogg";
    if (mime_type == "audio/mpeg") return ".mp3";
    if (mime_type == "audio/mp4") return ".m4a";

    if (media_type == "image") return ".jpg";
    if (media_type == "voice") return ".ogg";
    if (media_type == "audio") return ".mp3";
    if (media_type == "file") return "";
    return "";
}

}  // namespace kabot::channels
