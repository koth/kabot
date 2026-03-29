#include "media/silk_transcoder.hpp"
#include "media/silk/silk_decoder.hpp"

#include <fstream>
#include <vector>

namespace weixin::media {

bool SilkTranscoder::TranscodeToWav(const std::string& silk_path,
                                    const std::string& wav_path) {
  // Read SILK file
  std::ifstream silk_file(silk_path, std::ios::binary);
  if (!silk_file) {
    return false;
  }
  
  std::vector<uint8_t> silk_data((std::istreambuf_iterator<char>(silk_file)),
                                   std::istreambuf_iterator<char>());
  
  std::vector<uint8_t> wav_data;
  if (!TranscodeToWavBytes(silk_data, wav_data)) {
    return false;
  }
  
  // Write WAV file
  std::ofstream wav_file(wav_path, std::ios::binary);
  if (!wav_file) {
    return false;
  }
  
  wav_file.write(reinterpret_cast<const char*>(wav_data.data()), 
                 wav_data.size());
  
  return wav_file.good();
}

bool SilkTranscoder::TranscodeToWavBytes(const std::vector<uint8_t>& silk_data,
                                         std::vector<uint8_t>& wav_data) {
  // Decode SILK to PCM
  auto pcm_data = silk::SilkDecoder::Decode(silk_data, 24000);
  if (!pcm_data.has_value()) {
    return false;
  }
  
  // Wrap PCM in WAV container
  wav_data = silk::CreateWavContainer(pcm_data.value(), 24000, 1);
  
  return !wav_data.empty();
}

} // namespace weixin::media
