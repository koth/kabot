#pragma once

#include <cstdint>
#include <vector>
#include <optional>

namespace weixin::media::silk {

// SILK decoder wrapper
class SilkDecoder {
public:
  // Decode SILK data to PCM (16-bit signed)
  // sample_rate: typically 24000 for WeChat
  static std::optional<std::vector<int16_t>> Decode(
      const std::vector<uint8_t>& silk_data,
      int sample_rate = 24000);
  
  // Check if data is valid SILK
  static bool IsValidSilk(const std::vector<uint8_t>& data);
};

// Create WAV container from PCM data
std::vector<uint8_t> CreateWavContainer(
    const std::vector<int16_t>& pcm_data,
    int sample_rate,
    int channels);

} // namespace weixin::media::silk
