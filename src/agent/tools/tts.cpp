#include "agent/tools/tts.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <string_view>

#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

#include <openssl/err.h>
#include <openssl/sha.h>

#include "nlohmann/json.hpp"
#include "sandbox/sandbox_executor.hpp"

namespace kabot::agent::tools {
namespace {

constexpr const char* kTrustedClientToken = "6A5AA1D4EAFF4E9FB37E23D68491D6F4";
constexpr const char* kChromiumFullVersion = "143.0.3650.75";
constexpr unsigned long long kWindowsFileTimeEpoch = 11644473600ULL;

struct SubtitleLine {
    std::string part;
    long long start_ms = 0;
    long long end_ms = 0;
};

std::string GetParam(const std::unordered_map<std::string, std::string>& params,
                     const std::string& name) {
    auto it = params.find(name);
    if (it == params.end()) {
        return {};
    }
    return it->second;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ParseBool(const std::string& value, bool fallback = false) {
    if (value.empty()) {
        return fallback;
    }
    const auto lowered = ToLower(value);
    return lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "y";
}

std::string EscapeXml(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string Sha256HexUpper(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, input.data(), input.size());
    SHA256_Final(hash, &ctx);
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setfill('0');
    for (unsigned char byte : hash) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::string GenerateSecMsGecToken() {
    const auto now = static_cast<unsigned long long>(std::time(nullptr));
    const auto ticks = (now + kWindowsFileTimeEpoch) * 10000000ULL;
    const auto rounded = ticks - (ticks % 3000000000ULL);
    const auto to_hash = std::to_string(rounded) + kTrustedClientToken;
    return Sha256HexUpper(to_hash);
}

std::string RandomHex(std::size_t bytes) {
    std::vector<unsigned char> data(bytes);
    std::random_device rd;
    for (auto& b : data) {
        b = static_cast<unsigned char>(rd());
    }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto b : data) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

std::string DefaultAudioPath(const std::string& workspace) {
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ostringstream oss;
    oss << workspace << "/tts_out/" << stamp << ".mp3";
    return oss.str();
}

void WriteSubtitles(const std::string& path, const std::vector<SubtitleLine>& subs) {
    nlohmann::json json = nlohmann::json::array();
    for (const auto& line : subs) {
        json.push_back({
            {"part", line.part},
            {"start", line.start_ms},
            {"end", line.end_ms}
        });
    }
    std::ofstream output(path, std::ios::trunc);
    output << json.dump(2);
}

}  // namespace

EdgeTtsTool::EdgeTtsTool(std::string workspace)
    : workspace_(std::move(workspace)) {}

std::string EdgeTtsTool::ParametersJson() const {
    return R"({"type":"object","properties":{"text":{"type":"string"},"file":{"type":"string","description":"local text file path"},"voice":{"type":"string"},"lang":{"type":"string"},"output_format":{"type":"string"},"rate":{"type":"string"},"pitch":{"type":"string"},"volume":{"type":"string"},"save_subtitles":{"type":"boolean"},"audio_path":{"type":"string"},"auto_play":{"type":"boolean"},"timeout_ms":{"type":"integer"}},"required":[]})";
}

std::string EdgeTtsTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    auto text = GetParam(params, "text");
    const auto file_path = GetParam(params, "file");
    if (!file_path.empty()) {
        std::ifstream input(file_path);
        if (!input.is_open()) {
            return "Error: failed to open file";
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        text = buffer.str();
    }
    if (text.empty()) {
        return "Error: text or file is required";
    }
    const auto voice = GetParam(params, "voice").empty() ? "zh-CN-XiaoyiNeural" : GetParam(params, "voice");
    const auto lang = GetParam(params, "lang").empty() ? "zh-CN" : GetParam(params, "lang");
    const auto output_format = GetParam(params, "output_format").empty()
        ? "audio-24khz-48kbitrate-mono-mp3"
        : GetParam(params, "output_format");
    const auto rate = GetParam(params, "rate").empty() ? "default" : GetParam(params, "rate");
    const auto pitch = GetParam(params, "pitch").empty() ? "default" : GetParam(params, "pitch");
    const auto volume = GetParam(params, "volume").empty() ? "default" : GetParam(params, "volume");
    const bool save_subtitles = ParseBool(GetParam(params, "save_subtitles"), false);
    const bool auto_play = ParseBool(GetParam(params, "auto_play"), false);
    const bool use_default_path = GetParam(params, "audio_path").empty();
    auto audio_path = use_default_path
        ? DefaultAudioPath(workspace_)
        : GetParam(params, "audio_path");

    std::ofstream audio_out(audio_path, std::ios::binary | std::ios::trunc);
    if (!audio_out.is_open()) {
        return "Error: failed to open audio_path";
    }

    const auto sec_ms_gec = GenerateSecMsGecToken();
    const std::string host = "speech.platform.bing.com";
    const std::string target = "/consumer/speech/synthesize/readaloud/edge/v1?TrustedClientToken="
        + std::string(kTrustedClientToken)
        + "&Sec-MS-GEC=" + sec_ms_gec
        + "&Sec-MS-GEC-Version=1-" + kChromiumFullVersion;

    std::string stage = "init";
    try {
        boost::asio::io_context ioc;
        boost::asio::ssl::context ctx(boost::asio::ssl::context::tls_client);
        ctx.set_default_verify_paths();
        ctx.set_options(boost::asio::ssl::context::default_workarounds);
        ctx.set_verify_mode(boost::asio::ssl::verify_peer);
        boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>> ws(ioc, ctx);
        boost::asio::ip::tcp::resolver resolver(ioc);
        stage = "resolve";
        auto results = resolver.resolve(boost::asio::ip::tcp::v4(), host, "443");
        stage = "connect";
        boost::beast::get_lowest_layer(ws).connect(results);
        stage = "sni";
        if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str())) {
            const auto err = static_cast<int>(::ERR_get_error());
            boost::system::error_code ec(err, boost::asio::error::get_ssl_category());
            throw boost::system::system_error(ec);
        }
        stage = "tls_handshake";
        ws.next_layer().handshake(boost::asio::ssl::stream_base::client);

        stage = "ws_setup";
        ws.set_option(boost::beast::websocket::stream_base::decorator(
            [&](boost::beast::websocket::request_type& req) {
                req.set(boost::beast::http::field::host, host);
                req.set(boost::beast::http::field::user_agent,
                        std::string("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/")
                            + kChromiumFullVersion + " Safari/537.36 Edg/" + kChromiumFullVersion);
                req.set(boost::beast::http::field::pragma, "no-cache");
                req.set(boost::beast::http::field::cache_control, "no-cache");
                req.set(boost::beast::http::field::origin, "chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold");
                req.set(boost::beast::http::field::accept_language, "en-US,en;q=0.9");
            }));

        stage = "ws_handshake";
        ws.handshake(host, target);

        std::ostringstream config_msg;
        config_msg << "Content-Type:application/json; charset=utf-8\r\nPath:speech.config\r\n\r\n";
        config_msg << "{\"context\":{\"synthesis\":{\"audio\":{\"metadataoptions\":{\"sentenceBoundaryEnabled\":\"false\",\"wordBoundaryEnabled\":\"true\"},\"outputFormat\":\"";
        config_msg << output_format;
        config_msg << "\"}}}}";
        stage = "send_config";
        ws.write(boost::asio::buffer(config_msg.str()));

        const auto request_id = RandomHex(16);
        std::ostringstream ssml;
        ssml << "X-RequestId:" << request_id << "\r\n";
        ssml << "Content-Type:application/ssml+xml\r\nPath:ssml\r\n\r\n";
        ssml << "<speak version=\"1.0\" xmlns=\"http://www.w3.org/2001/10/synthesis\" "
             << "xmlns:mstts=\"https://www.w3.org/2001/mstts\" xml:lang=\"" << lang << "\">"
             << "<voice name=\"" << voice << "\"><prosody rate=\"" << rate << "\" pitch=\"" << pitch
             << "\" volume=\"" << volume << "\">" << EscapeXml(text) << "</prosody></voice></speak>";
        stage = "send_ssml";
        ws.write(boost::asio::buffer(ssml.str()));

        std::vector<SubtitleLine> subtitles;
        bool finished = false;
        while (!finished) {
            stage = "read";
            boost::beast::flat_buffer buffer;
            ws.read(buffer);
            if (ws.got_text()) {
                const auto message = boost::beast::buffers_to_string(buffer.data());
                if (message.find("Path:turn.end") != std::string::npos) {
                    finished = true;
                } else if (message.find("Path:audio.metadata") != std::string::npos) {
                    const auto pos = message.rfind("\r\n");
                    if (pos != std::string::npos) {
                        const auto payload = message.substr(pos + 2);
                        auto json = nlohmann::json::parse(payload, nullptr, false);
                        if (json.is_object() && json.contains("Metadata")) {
                            for (const auto& item : json["Metadata"]) {
                                if (!item.contains("Data")) {
                                    continue;
                                }
                                const auto& data = item["Data"];
                                if (!data.contains("text")) {
                                    continue;
                                }
                                const auto& text_obj = data["text"];
                                SubtitleLine line;
                                line.part = text_obj.value("Text", "");
                                line.start_ms = static_cast<long long>(data.value("Offset", 0)) / 10000;
                                line.end_ms = static_cast<long long>(data.value("Offset", 0) + data.value("Duration", 0)) / 10000;
                                subtitles.push_back(std::move(line));
                            }
                        }
                    }
                }
            } else {
                const auto data = buffer.data();
                const std::string separator = "Path:audio\r\n";
                const std::size_t size = boost::asio::buffer_size(data);
                std::string storage(size, '\0');
                boost::asio::buffer_copy(boost::asio::buffer(storage), data);
                std::string_view view(storage.data(), storage.size());
                const auto index = view.find(separator);
                if (index != std::string_view::npos) {
                    const auto audio_start = index + separator.size();
                    audio_out.write(storage.data() + audio_start,
                                    static_cast<std::streamsize>(size - audio_start));
                }
            }
        }

        stage = "close";
        boost::system::error_code close_ec;
        ws.close(boost::beast::websocket::close_code::normal, close_ec);

        audio_out.flush();
        audio_out.close();

        std::string subtitle_path;
        if (save_subtitles) {
            subtitle_path = audio_path + ".json";
            WriteSubtitles(subtitle_path, subtitles);
        }

        nlohmann::json autoplay_result = nullptr;
        bool audio_deleted = false;
        if (use_default_path) {
            std::filesystem::path source_path(audio_path);
            std::filesystem::path opus_path = source_path;
            opus_path.replace_extension(".opus");
            const std::string command = "ffmpeg -y -i \"" + audio_path
                + "\" -c:a libopus -b:a 48k \"" + opus_path.string() + "\"";
            const auto exec_result = kabot::sandbox::SandboxExecutor::Run(
                command,
                workspace_,
                std::chrono::seconds(240));
            if (!exec_result.timed_out && !exec_result.blocked && exec_result.exit_code == 0) {
                std::error_code remove_ec;
                std::filesystem::remove(audio_path, remove_ec);
                audio_path = opus_path.string();
            } else {
                std::cerr << "[tts] opus convert failed: "
                          << (exec_result.error.empty() ? "unknown" : exec_result.error)
                          << std::endl;
            }
        }
        if (auto_play) {
            const std::string command = "ffplay -nodisp -autoexit -hide_banner -loglevel error \"" +
                audio_path + "\"";
            const auto exec_result = kabot::sandbox::SandboxExecutor::Run(
                command,
                workspace_,
                std::chrono::seconds(240));
            autoplay_result = nlohmann::json{
                {"exit_code", exec_result.exit_code},
                {"timed_out", exec_result.timed_out},
                {"blocked", exec_result.blocked},
                {"stderr", exec_result.error.empty() ? nlohmann::json(nullptr)
                                                     : nlohmann::json(exec_result.error)}
            };
            if (!exec_result.timed_out && !exec_result.blocked && exec_result.exit_code == 0) {
                std::error_code remove_ec;
                audio_deleted = std::filesystem::remove(audio_path, remove_ec);
            }
        }

        const auto audio_path_json = auto_play ? nlohmann::json(nullptr) : nlohmann::json(audio_path);
        nlohmann::json result = {
            {"audio_path", audio_path_json},
            {"subtitle_path", subtitle_path.empty() ? nlohmann::json(nullptr) : nlohmann::json(subtitle_path)},
            {"auto_play", auto_play},
            {"auto_play_result", autoplay_result},
            {"audio_deleted", audio_deleted}
        };
        return result.dump(2);
    } catch (const std::exception& ex) {
        return std::string("Error: tts failed at ") + stage + ": " + ex.what();
    }
}

}  // namespace kabot::agent::tools
