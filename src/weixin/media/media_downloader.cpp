#include "media/media_downloader.hpp"
#include "media/mime_detector.hpp"
#include "media/silk_transcoder.hpp"
#include "cdn/pic_decrypt.hpp"
#include "util/random.hpp"

#include <fstream>
#include <filesystem>
#include <iostream>

namespace weixin::media {

std::optional<std::string> DownloadMedia(
    const std::string& cdn_url,
    const std::string& encrypt_param,
    const std::string& aes_key,
    const std::string& output_dir,
    const std::string& filename) {
  // Download and decrypt
  auto data = cdn::DownloadAndDecrypt(cdn_url, encrypt_param, aes_key);
  if (!data.has_value()) {
    return std::nullopt;
  }
  
  // Create output directory if needed
  std::filesystem::create_directories(output_dir);
  
  // Determine file extension from content
  std::string ext = ".bin";
  std::string mime = DetectMimeTypeFromContent(data.value());
  if (!mime.empty() && mime != "application/octet-stream") {
    ext = GetExtensionFromMime(mime);
  }
  
  // Construct full path
  std::string filepath = output_dir + "/" + filename + ext;
  
  // Write to file
  std::ofstream file(filepath, std::ios::binary);
  if (!file) {
    return std::nullopt;
  }
  
  file.write(reinterpret_cast<const char*>(data->data()), 
             static_cast<std::streamsize>(data->size()));
  
  return filepath;
}

std::optional<std::string> DownloadVoice(
    const std::string& cdn_url,
    const std::string& encrypt_param,
    const std::string& aes_key,
    const std::string& output_dir) {
  // Download SILK file
  auto silk_data = cdn::DownloadAndDecrypt(cdn_url, encrypt_param, aes_key);
  if (!silk_data.has_value()) {
    return std::nullopt;
  }
  
  // Create temp file for SILK
  std::filesystem::create_directories(output_dir);
  std::string temp_silk = output_dir + "/" + util::GenerateTempFilename("voice") + ".silk";
  
  {
    std::ofstream file(temp_silk, std::ios::binary);
    file.write(reinterpret_cast<const char*>(silk_data->data()),
               static_cast<std::streamsize>(silk_data->size()));
  }
  
  // Transcode to WAV
  std::string wav_path = output_dir + "/" + util::GenerateTempFilename("voice") + ".wav";
  if (SilkTranscoder::TranscodeToWav(temp_silk, wav_path)) {
    // Clean up temp SILK file
    std::filesystem::remove(temp_silk);
    return wav_path;
  }
  
  // If transcoding fails, return SILK file
  return temp_silk;
}

std::optional<std::string> DownloadImage(
    const std::string& cdn_url,
    const std::string& encrypt_param,
    const std::string& aes_key,
    const std::string& output_dir) {
  std::string filename = util::GenerateTempFilename("img");
  return DownloadMedia(cdn_url, encrypt_param, aes_key, output_dir, filename);
}

std::optional<std::string> DownloadFile(
    const std::string& cdn_url,
    const std::string& encrypt_param,
    const std::string& aes_key,
    const std::string& output_dir,
    const std::string& filename) {
  return DownloadMedia(cdn_url, encrypt_param, aes_key, output_dir, filename);
}

} // namespace weixin::media
