#pragma once

#include <cstdint>
#include <vector>
#include <optional>

namespace weixin::media::silk {

// Decode SILK to PCM using native SDK
std::optional<std::vector<int16_t>> DecodeSilkToPcm(
    const std::vector<uint8_t>& silk_data,
    int sample_rate);

} // namespace weixin::media::silk
