#include "cdn/aes_ecb.hpp"

#include <openssl/evp.h>
#include <openssl/err.h>
#include <string>

namespace weixin::cdn {

std::vector<uint8_t> AESECB::Encrypt(const std::vector<uint8_t>& data,
                                         const std::vector<uint8_t>& key) {
  if (key.size() != kBlockSize) {
    return {};
  }
  
  size_t padded_size = CalculatePaddedSize(data.size());
  std::vector<uint8_t> padded_data(padded_size);
  
  // Copy original data
  std::memcpy(padded_data.data(), data.data(), data.size());
  
  // PKCS7 padding
  uint8_t padding_value = static_cast<uint8_t>(padded_size - data.size());
  for (size_t i = data.size(); i < padded_size; ++i) {
    padded_data[i] = padding_value;
  }
  
  std::vector<uint8_t> result(padded_size);
  
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    return {};
  }
  
  // Initialize encryption with AES-128-ECB
  if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key.data(), nullptr) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return {};
  }
  
  // Disable padding as we handle it manually
  EVP_CIPHER_CTX_set_padding(ctx, 0);
  
  int len;
  if (EVP_EncryptUpdate(ctx, result.data(), &len, 
                        padded_data.data(), static_cast<int>(padded_data.size())) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return {};
  }
  
  int final_len;
  if (EVP_EncryptFinal_ex(ctx, result.data() + len, &final_len) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return {};
  }
  
  EVP_CIPHER_CTX_free(ctx);
  return result;
}

std::vector<uint8_t> AESECB::Decrypt(const std::vector<uint8_t>& data,
                                         const std::vector<uint8_t>& key) {
  if (key.size() != kBlockSize || data.size() % kBlockSize != 0) {
    return {};
  }
  
  std::vector<uint8_t> result(data.size());
  
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    return {};
  }
  
  // Initialize decryption with AES-128-ECB
  if (EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key.data(), nullptr) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return {};
  }
  
  // Disable padding as we handle it manually
  EVP_CIPHER_CTX_set_padding(ctx, 0);
  
  int len;
  if (EVP_DecryptUpdate(ctx, result.data(), &len,
                        data.data(), static_cast<int>(data.size())) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return {};
  }
  
  int final_len;
  if (EVP_DecryptFinal_ex(ctx, result.data() + len, &final_len) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return {};
  }
  
  EVP_CIPHER_CTX_free(ctx);
  
  // Remove PKCS7 padding
  uint8_t padding_value = result.back();
  if (padding_value > 0 && padding_value <= kBlockSize) {
    result.resize(result.size() - padding_value);
  }
  
  return result;
}

size_t AESECB::CalculatePaddedSize(size_t data_size) {
  return ((data_size / kBlockSize) + 1) * kBlockSize;
}

} // namespace weixin::cdn
