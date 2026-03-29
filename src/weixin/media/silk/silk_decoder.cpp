#include "media/silk/silk_decoder.hpp"
#include "media/silk/silk_sdk_wrapper.hpp"

#include <cstring>
#include <string>

namespace weixin::media::silk {

std::optional<std::vector<int16_t>> SilkDecoder::Decode(
    const std::vector<uint8_t>& silk_data,
    int sample_rate) {
  return DecodeSilkToPcm(silk_data, sample_rate);
}

bool SilkDecoder::IsValidSilk(const std::vector<uint8_t>& data) {
  if (data.size() < 10) {
    return false;
  }
  
  // Check for SILK magic bytes
  // SILK files typically start with "\x02#!SILK_V3"
  const char* magic = "#!SILK_V3";
  if (data[0] == 0x02) {
    return std::memcmp(data.data() + 1, magic, 9) == 0;
  }
  
  return false;
}

std::vector<uint8_t> CreateWavContainer(
    const std::vector<int16_t>& pcm_data,
    int sample_rate,
    int channels) {
  std::vector<uint8_t> wav;
  
  // WAV header constants
  const int bits_per_sample = 16;
  const int byte_rate = sample_rate * channels * bits_per_sample / 8;
  const int block_align = channels * bits_per_sample / 8;
  const int data_size = static_cast<int>(pcm_data.size() * sizeof(int16_t));
  const int file_size = 36 + data_size;
  
  // RIFF chunk
  wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
  wav.insert(wav.end(), reinterpret_cast<const uint8_t*>(&file_size), reinterpret_cast<const uint8_t*>(&file_size) + 4);
  wav.insert(wav.end(), {'W', 'A', 'V', 'E'});
  
  // fmt chunk
  wav.insert(wav.end(), {'f', 'm', 't', ' '});
  const int subchunk1_size = 16;
  wav.insert(wav.end(), reinterpret_cast<const uint8_t*>(&subchunk1_size), reinterpret_cast<const uint8_t*>(&subchunk1_size) + 4);
  const int16_t audio_format = 1; // PCM
  wav.insert(wav.end(), reinterpret_cast<const uint8_t*>(&audio_format), reinterpret_cast<const uint8_t*>(&audio_format) + 2);
  const int16_t num_channels = static_cast<int16_t>(channels);
  wav.insert(wav.end(), reinterpret_cast<const uint8_t*>(&num_channels), reinterpret_cast<const uint8_t*>(&num_channels) + 2);
  wav.insert(wav.end(), reinterpret_cast<const uint8_t*>(&sample_rate), reinterpret_cast<const uint8_t*>(&sample_rate) + 4);
  wav.insert(wav.end(), reinterpret_cast<const uint8_t*>(&byte_rate), reinterpret_cast<const uint8_t*>(&byte_rate) + 4);
  const int16_t block_align_val = static_cast<int16_t>(block_align);
  wav.insert(wav.end(), reinterpret_cast<const uint8_t*>(&block_align_val), reinterpret_cast<const uint8_t*>(&block_align_val) + 2);
  const int16_t bits_per_sample_val = static_cast<int16_t>(bits_per_sample);
  wav.insert(wav.end(), reinterpret_cast<const uint8_t*>(&bits_per_sample_val), reinterpret_cast<const uint8_t*>(&bits_per_sample_val) + 2);
  
  // data chunk
  wav.insert(wav.end(), {'d', 'a', 't', 'a'});
  wav.insert(wav.end(), reinterpret_cast<const uint8_t*>(&data_size), reinterpret_cast<const uint8_t*>(&data_size) + 4);
  wav.insert(wav.end(), reinterpret_cast<const uint8_t*>(pcm_data.data()), 
             reinterpret_cast<const uint8_t*>(pcm_data.data() + pcm_data.size()));
  
  return wav;
}

} // namespace weixin::media::silk
