#include "crypto_utils.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

namespace oemail {
namespace crypto_utils {

std::string base64_encode(const std::vector<uint8_t>& input) {
    if (input.empty()) return "";
    int expected_len = 4 * ((input.size() + 2) / 3);
    std::string output(expected_len + 1, '\0');
    int len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(output.data()), input.data(), input.size());
    output.resize(len);
    return output;
}

std::vector<uint8_t> base64_decode(const std::string& input) {
    if (input.empty()) return {};
    std::vector<uint8_t> output(3 * input.size() / 4 + 3); // add padding space
    int len = EVP_DecodeBlock(output.data(), reinterpret_cast<const unsigned char*>(input.data()), input.size());
    if (len < 0) return {}; // handle decoding error
    if (input.size() > 0 && input[input.size() - 1] == '=') len--;
    if (input.size() > 1 && input[input.size() - 2] == '=') len--;
    if (len < 0) len = 0;
    output.resize(len);
    return output;
}

std::string derive_master_key(const std::string& password) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(password.c_str()), password.length(), hash);
    return std::string(reinterpret_cast<char*>(hash), SHA256_DIGEST_LENGTH);
}

std::string generate_random_phrase() {
    unsigned char buf[32];
    RAND_bytes(buf, 32);
    return std::string(reinterpret_cast<char*>(buf), 32);
}

} // namespace crypto_utils
} // namespace oemail
