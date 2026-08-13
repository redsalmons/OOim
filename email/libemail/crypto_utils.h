#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <string>
#include <vector>
#include <cstdint>

namespace oemail {
namespace crypto_utils {

// Base64 encoding
std::string base64_encode(const std::vector<uint8_t>& input);

// Base64 decoding
std::vector<uint8_t> base64_decode(const std::string& input);

// Derive 32-byte master key from password using SHA-256
std::string derive_master_key(const std::string& password);

// Generate random 32-byte phrase
std::string generate_random_phrase();

} // namespace crypto_utils
} // namespace oemail

#endif // CRYPTO_UTILS_H
