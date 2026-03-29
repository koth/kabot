#pragma once

#include <string>
#include <optional>

namespace weixin::media {

// Download media from CDN and save to file
// Returns the saved file path on success
std::optional<std::string> DownloadMedia(
    const std::string& cdn_url,
    const std::string& encrypt_param,
    const std::string& aes_key,
    const std::string& output_dir,
    const std::string& filename);

// Download voice message and transcode to WAV
std::optional<std::string> DownloadVoice(
    const std::string& cdn_url,
    const std::string& encrypt_param,
    const std::string& aes_key,
    const std::string& output_dir);

// Download image
std::optional<std::string> DownloadImage(
    const std::string& cdn_url,
    const std::string& encrypt_param,
    const std::string& aes_key,
    const std::string& output_dir);

// Download file attachment
std::optional<std::string> DownloadFile(
    const std::string& cdn_url,
    const std::string& encrypt_param,
    const std::string& aes_key,
    const std::string& output_dir,
    const std::string& filename);

} // namespace weixin::media
