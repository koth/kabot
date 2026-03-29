#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace weixin::media {

// SILK to WAV transcoder
class SilkTranscoder {
public:
  // Transcode SILK file to WAV
  static bool TranscodeToWav(const std::string& silk_path,
                             const std::string& wav_path);
  
  // Transcode SILK data in memory to WAV
  static bool TranscodeToWavBytes(const std::vector<uint8_t>& silk_data,
                                  std::vector<uint8_t>& wav_data);
};

} // namespace weixin::media
