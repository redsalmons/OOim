#include "crypto_aes.h"
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <cstring>
#include <stdexcept>
#include <memory>

namespace oemail {

class CryptoAES::Impl {
public:
    Impl() : initialized_(false), enc_ctx_(nullptr), dec_ctx_(nullptr) {
        // Initialize OpenSSL
        OpenSSL_add_all_algorithms();
    }
    
    ~Impl() {
        EVP_CIPHER_CTX_free(enc_ctx_);
        EVP_CIPHER_CTX_free(dec_ctx_);
    }
    
    void set_key(const std::string& key) {
        if (key.length() != 32) {
            last_error_ = "Key must be 32 bytes (256 bits) for AES-256";
            return;
        }
        key_ = key;
        initialized_ = true;
        last_error_.clear();
    }
    
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data) {
        if (!initialized_) {
            last_error_ = "Key not set";
            return {};
        }
        
        // Generate random IV
        unsigned char iv[AES_BLOCK_SIZE];
        if (RAND_bytes(iv, AES_BLOCK_SIZE) != 1) {
            last_error_ = "Failed to generate IV";
            return {};
        }
        
        // Create encryption context
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            last_error_ = "Failed to create cipher context";
            return {};
        }
        
        // Initialize encryption
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, 
                               reinterpret_cast<const unsigned char*>(key_.data()), iv) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            last_error_ = "Failed to initialize encryption";
            return {};
        }
        
        // Prepare output buffer (IV + encrypted data + padding)
        int max_output_len = AES_BLOCK_SIZE + data.size() + AES_BLOCK_SIZE;
        std::vector<uint8_t> output(max_output_len);
        
        // Add IV to output
        std::memcpy(output.data(), iv, AES_BLOCK_SIZE);
        
        // Encrypt data
        int len;
        int ciphertext_len = 0;
        
        if (EVP_EncryptUpdate(ctx, output.data() + AES_BLOCK_SIZE, &len,
                              data.data(), data.size()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            last_error_ = "Failed to encrypt data";
            return {};
        }
        ciphertext_len = len;
        
        // Finalize encryption
        if (EVP_EncryptFinal_ex(ctx, output.data() + AES_BLOCK_SIZE + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            last_error_ = "Failed to finalize encryption";
            return {};
        }
        ciphertext_len += len;
        
        output.resize(AES_BLOCK_SIZE + ciphertext_len);
        
        EVP_CIPHER_CTX_free(ctx);
        last_error_.clear();
        return output;
    }
    
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& encrypted_data) {
        if (!initialized_) {
            last_error_ = "Key not set";
            return {};
        }
        
        if (encrypted_data.size() < AES_BLOCK_SIZE) {
            last_error_ = "Encrypted data too short";
            return {};
        }
        
        // Extract IV from the beginning
        unsigned char iv[AES_BLOCK_SIZE];
        std::memcpy(iv, encrypted_data.data(), AES_BLOCK_SIZE);
        
        // Create decryption context
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            last_error_ = "Failed to create cipher context";
            return {};
        }
        
        // Initialize decryption
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                               reinterpret_cast<const unsigned char*>(key_.data()), iv) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            last_error_ = "Failed to initialize decryption";
            return {};
        }
        
        // Prepare output buffer (same size as encrypted data minus IV plus block size for padding)
        int ciphertext_len = encrypted_data.size() - AES_BLOCK_SIZE;
        std::vector<uint8_t> output(ciphertext_len + AES_BLOCK_SIZE);
        
        // Decrypt data
        int len;
        int plaintext_len = 0;
        
        if (EVP_DecryptUpdate(ctx, output.data(), &len,
                              encrypted_data.data() + AES_BLOCK_SIZE,
                              ciphertext_len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            last_error_ = "Failed to decrypt data";
            return {};
        }
        plaintext_len = len;
        
        // Finalize decryption (this handles padding removal)
        if (EVP_DecryptFinal_ex(ctx, output.data() + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            last_error_ = "Failed to finalize decryption";
            return {};
        }
        plaintext_len += len;
        
        output.resize(plaintext_len);
        
        EVP_CIPHER_CTX_free(ctx);
        last_error_.clear();
        return output;
    }
    
    std::string get_last_error() const {
        return last_error_;
    }

private:
    std::string key_;
    std::string last_error_;
    bool initialized_;
    EVP_CIPHER_CTX* enc_ctx_;
    EVP_CIPHER_CTX* dec_ctx_;
};

CryptoAES::CryptoAES() : p_impl(std::make_unique<Impl>()) {}

CryptoAES::~CryptoAES() = default;

std::vector<uint8_t> CryptoAES::encrypt(const std::vector<uint8_t>& data) {
    return p_impl->encrypt(data);
}

std::vector<uint8_t> CryptoAES::decrypt(const std::vector<uint8_t>& encrypted_data) {
    return p_impl->decrypt(encrypted_data);
}

void CryptoAES::set_key(const std::string& key) {
    p_impl->set_key(key);
}

std::string CryptoAES::get_last_error() const {
    return p_impl->get_last_error();
}

} // namespace oemail
