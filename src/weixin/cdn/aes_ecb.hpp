#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace weixin::cdn {

// AES-128-ECB encryption/decryption
class AESECB {
public:
  // Encrypt data with AES-128-ECB and PKCS7 padding
  static std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& data,
                                         const std::vector<uint8_t>& key);
  
  // Decrypt data with AES-128-ECB
  static std::vector<uint8_t> Decrypt(const std::vector<uint8_t>& data,
                                         const std::vector<uint8_t>& key);
  
  // Calculate PKCS7 padded size
  static size_t CalculatePaddedSize(size_t data_size);
  
private:
  static constexpr size_t kBlockSize = 16;  // AES block size
};

} // namespace weixin::cdn
