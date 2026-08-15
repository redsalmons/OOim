#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include "email_handler_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <random>
#include <sstream>

#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <vector>
#include <nlohmann/json.hpp>
#include <zlib.h>

// Shared global state
sqlite3* g_db = NULL;
std::mutex g_db_mutex;

static int system_initialized = 0;

// Unified log function - callable from Dart via FFI
extern "C" void email_log_write(const char* message) {
    if (message) {
        LOG_INFO("%s", message);
    }
}

// Initialize logger only - call once at app startup
extern "C" int email_logger_init(const char* logDir) {
    if (!logDir) return -1;
    oemail::Logger::getInstance().init(logDir);
    LOG_INFO("Logger initialized with logDir: %s", logDir);
    return 0;
}

// Initialize libemail system with paths provided from Dart
int email_core_initialize(const char* appSupportDir) {
    if (!appSupportDir) {
        return -1;
    }

    char dataDir[512], configDir[512], logDir[512];
    snprintf(dataDir, sizeof(dataDir), "%s/data", appSupportDir);
    snprintf(configDir, sizeof(configDir), "%s/config", appSupportDir);
    snprintf(logDir, sizeof(logDir), "%s/log", appSupportDir);

    LOG_INFO("Initializing libemail system with dataDir: %s, configDir: %s, logDir: %s", dataDir, configDir, logDir);
    int initResult = systemOpen_c(dataDir, configDir, logDir);
    LOG_INFO("systemOpen_c result: %d", initResult);

    return initResult;
}

std::string generate_email_id(const char* type) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(10000000, 99999999);
    std::ostringstream oss;
    oss << dis(gen) << "." << (type ? type : "");
    return oss.str();
}

// Generate ECC P-256 key pair, output PEM-encoded public key and AES-256-CBC encrypted private key
bool generate_ecc_keypair(std::string& outPubPem, std::string& outPrivPem, const std::string& password) {
    EVP_PKEY* pkey = EVP_EC_gen("P-256");
    if (!pkey) return false;

    // Export public key to PEM
    BIO* pubBio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(pubBio, pkey);
    char* pubData = NULL;
    long pubLen = BIO_get_mem_data(pubBio, &pubData);
    outPubPem.assign(pubData, pubLen);
    BIO_free(pubBio);

    // Export private key to PEM with AES-256-CBC encryption using password
    BIO* privBio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(privBio, pkey, EVP_aes_256_cbc(),
                             (unsigned char*)password.c_str(), (int)password.length(),
                             NULL, NULL);
    char* privData = NULL;
    long privLen = BIO_get_mem_data(privBio, &privData);
    outPrivPem.assign(privData, privLen);
    BIO_free(privBio);

    EVP_PKEY_free(pkey);
    return true;
}

// Generate a random alphanumeric password of given length
std::string generate_random_password(int length) {
    static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, (int)(sizeof(charset) - 2));
    std::string result;
    result.reserve(length);
    for (int i = 0; i < length; ++i) {
        result += charset[dis(gen)];
    }
    return result;
}

// Base64 encode
std::string base64_encode(const unsigned char* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int val = 0, valb = -6;
    for (size_t i = 0; i < len; i++) {
        val = (val << 8) + data[i];
        valb += 8;
        while (valb >= 0) {
            result.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) result.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (result.size() % 4) result.push_back('=');
    return result;
}

// Compute MD5 hash of input string, return hex-encoded string
std::string compute_md5(const std::string& input) {
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) return "";

    unsigned char md5_value[EVP_MAX_MD_SIZE];
    unsigned int md5_len = 0;

    if (EVP_DigestInit_ex(mdctx, EVP_md5(), NULL) != 1) {
        EVP_MD_CTX_free(mdctx);
        return "";
    }
    if (EVP_DigestUpdate(mdctx, input.data(), input.size()) != 1) {
        EVP_MD_CTX_free(mdctx);
        return "";
    }
    if (EVP_DigestFinal_ex(mdctx, md5_value, &md5_len) != 1) {
        EVP_MD_CTX_free(mdctx);
        return "";
    }

    EVP_MD_CTX_free(mdctx);

    char hex[33];
    for (unsigned int i = 0; i < md5_len; i++) {
        snprintf(hex + i * 2, 3, "%02x", md5_value[i]);
    }
    return std::string(hex);
}

// Sign data with ECC private key (ECDSA), return base64-encoded signature
std::string sign_with_ecc_private_key(const std::string& privPem, const std::string& keyPassword, const std::string& data) {
    BIO* bio = BIO_new_mem_buf(privPem.data(), (int)privPem.size());
    if (!bio) return "";

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, (void*)keyPassword.c_str());
    BIO_free(bio);
    if (!pkey) {
        LOG_INFO("sign_with_ecc_private_key: failed to load private key\n");
        return "";
    }

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        EVP_PKEY_free(pkey);
        return "";
    }

    if (EVP_DigestSignInit(mdctx, NULL, EVP_sha256(), NULL, pkey) != 1) {
        LOG_INFO("sign_with_ecc_private_key: DigestSignInit failed\n");
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return "";
    }

    if (EVP_DigestSignUpdate(mdctx, data.data(), data.size()) != 1) {
        LOG_INFO("sign_with_ecc_private_key: DigestSignUpdate failed\n");
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return "";
    }

    size_t sigLen = 0;
    if (EVP_DigestSignFinal(mdctx, NULL, &sigLen) != 1) {
        LOG_INFO("sign_with_ecc_private_key: DigestSignFinal (get len) failed\n");
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return "";
    }

    std::vector<unsigned char> sig(sigLen);
    if (EVP_DigestSignFinal(mdctx, sig.data(), &sigLen) != 1) {
        LOG_INFO("sign_with_ecc_private_key: DigestSignFinal failed\n");
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return "";
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);

    return base64_encode(sig.data(), sigLen);
}

// Base64 decode
std::vector<uint8_t> base64_decode(const std::string& encoded) {
    static const int tbl[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };

    std::vector<uint8_t> result;
    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (tbl[c] == -1) continue;
        val = (val << 6) + tbl[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return result;
}

// ECC encrypt with public key (ECIES-like: generate ECDH shared secret, use as AES key)
std::string ecc_encrypt_with_public_key(const std::string& pubPem, const std::string& plaintext) {
    if (pubPem.empty() || plaintext.empty()) return "";

    BIO* bio = BIO_new_mem_buf(pubPem.data(), (int)pubPem.size());
    if (!bio) return "";
    EVP_PKEY* peerKey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!peerKey) return "";

    // Generate ephemeral key pair
    EVP_PKEY* ephemKey = EVP_EC_gen("P-256");
    if (!ephemKey) { EVP_PKEY_free(peerKey); return ""; }

    // Derive shared secret
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(ephemKey, NULL);
    if (!ctx) { EVP_PKEY_free(peerKey); EVP_PKEY_free(ephemKey); return ""; }

    if (EVP_PKEY_derive_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx); EVP_PKEY_free(peerKey); EVP_PKEY_free(ephemKey); return "";
    }
    if (EVP_PKEY_derive_set_peer(ctx, peerKey) <= 0) {
        EVP_PKEY_CTX_free(ctx); EVP_PKEY_free(peerKey); EVP_PKEY_free(ephemKey); return "";
    }

    size_t secretLen = 0;
    if (EVP_PKEY_derive(ctx, NULL, &secretLen) <= 0) {
        EVP_PKEY_CTX_free(ctx); EVP_PKEY_free(peerKey); EVP_PKEY_free(ephemKey); return "";
    }
    std::vector<uint8_t> sharedSecret(secretLen);
    if (EVP_PKEY_derive(ctx, sharedSecret.data(), &secretLen) <= 0) {
        EVP_PKEY_CTX_free(ctx); EVP_PKEY_free(peerKey); EVP_PKEY_free(ephemKey); return "";
    }
    sharedSecret.resize(secretLen);
    EVP_PKEY_CTX_free(ctx);

    // Export ephemeral public key to PEM
    BIO* ephBio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(ephBio, ephemKey);
    char* ephData = NULL;
    long ephLen = BIO_get_mem_data(ephBio, &ephData);
    std::string ephPubPem(ephData, ephLen);
    BIO_free(ephBio);
    EVP_PKEY_free(ephemKey);
    EVP_PKEY_free(peerKey);

    // Use SHA-256 of shared secret as AES-256 key
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    unsigned char hash[32];
    unsigned int hashLen = 0;
    EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(mdctx, sharedSecret.data(), sharedSecret.size());
    EVP_DigestFinal_ex(mdctx, hash, &hashLen);
    EVP_MD_CTX_free(mdctx);

    // AES-256-CBC encrypt the plaintext
    unsigned char iv[16];
    RAND_bytes(iv, 16);

    EVP_CIPHER_CTX* cipherCtx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(cipherCtx, EVP_aes_256_cbc(), NULL,
                       (const unsigned char*)hash, iv);

    std::vector<uint8_t> ciphertext(plaintext.size() + 32);
    int outLen = 0;
    EVP_EncryptUpdate(cipherCtx, ciphertext.data(), &outLen,
                      (const unsigned char*)plaintext.data(), plaintext.size());
    int totalLen = outLen;
    EVP_EncryptFinal_ex(cipherCtx, ciphertext.data() + outLen, &outLen);
    totalLen += outLen;
    EVP_CIPHER_CTX_free(cipherCtx);
    ciphertext.resize(totalLen);

    // Output format: base64(ephPubPem) + "|" + base64(iv + ciphertext)
    std::string ephB64 = base64_encode((const unsigned char*)ephPubPem.data(), ephPubPem.size());

    std::vector<uint8_t> ivAndCipher;
    ivAndCipher.insert(ivAndCipher.end(), iv, iv + 16);
    ivAndCipher.insert(ivAndCipher.end(), ciphertext.begin(), ciphertext.end());
    std::string dataB64 = base64_encode(ivAndCipher.data(), ivAndCipher.size());

    return ephB64 + "|" + dataB64;
}

// ECC decrypt with private key
std::string ecc_decrypt_with_private_key(const std::string& privPem, const std::string& keyPassword, const std::string& ciphertext) {
    if (privPem.empty() || ciphertext.empty()) return "";

    // Parse format: base64(ephPubPem) + "|" + base64(iv + ciphertext)
    size_t sep = ciphertext.find('|');
    if (sep == std::string::npos) return "";

    std::string ephB64 = ciphertext.substr(0, sep);
    std::string dataB64 = ciphertext.substr(sep + 1);

    auto ephPubRaw = base64_decode(ephB64);
    std::string ephPubPem(ephPubRaw.begin(), ephPubRaw.end());

    auto ivAndCipher = base64_decode(dataB64);
    if (ivAndCipher.size() < 16) return "";

    // Load private key
    BIO* privBio = BIO_new_mem_buf(privPem.data(), (int)privPem.size());
    if (!privBio) return "";
    EVP_PKEY* privKey = PEM_read_bio_PrivateKey(privBio, NULL, NULL,
                                                (void*)keyPassword.c_str());
    BIO_free(privBio);
    if (!privKey) return "";

    // Load ephemeral public key
    BIO* ephBio = BIO_new_mem_buf(ephPubPem.data(), (int)ephPubPem.size());
    if (!ephBio) { EVP_PKEY_free(privKey); return ""; }
    EVP_PKEY* ephKey = PEM_read_bio_PUBKEY(ephBio, NULL, NULL, NULL);
    BIO_free(ephBio);
    if (!ephKey) { EVP_PKEY_free(privKey); return ""; }

    // Derive shared secret
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(privKey, NULL);
    if (!ctx) { EVP_PKEY_free(privKey); EVP_PKEY_free(ephKey); return ""; }

    if (EVP_PKEY_derive_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx); EVP_PKEY_free(privKey); EVP_PKEY_free(ephKey); return "";
    }
    if (EVP_PKEY_derive_set_peer(ctx, ephKey) <= 0) {
        EVP_PKEY_CTX_free(ctx); EVP_PKEY_free(privKey); EVP_PKEY_free(ephKey); return "";
    }

    size_t secretLen = 0;
    if (EVP_PKEY_derive(ctx, NULL, &secretLen) <= 0) {
        EVP_PKEY_CTX_free(ctx); EVP_PKEY_free(privKey); EVP_PKEY_free(ephKey); return "";
    }
    std::vector<uint8_t> sharedSecret(secretLen);
    if (EVP_PKEY_derive(ctx, sharedSecret.data(), &secretLen) <= 0) {
        EVP_PKEY_CTX_free(ctx); EVP_PKEY_free(privKey); EVP_PKEY_free(ephKey); return "";
    }
    sharedSecret.resize(secretLen);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(privKey);
    EVP_PKEY_free(ephKey);

    // SHA-256 of shared secret as AES key
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    unsigned char hash[32];
    unsigned int hashLen = 0;
    EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(mdctx, sharedSecret.data(), sharedSecret.size());
    EVP_DigestFinal_ex(mdctx, hash, &hashLen);
    EVP_MD_CTX_free(mdctx);

    // AES-256-CBC decrypt
    unsigned char iv[16];
    memcpy(iv, ivAndCipher.data(), 16);

    EVP_CIPHER_CTX* cipherCtx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(cipherCtx, EVP_aes_256_cbc(), NULL,
                       (const unsigned char*)hash, iv);

    int cipherLen = (int)ivAndCipher.size() - 16;
    std::vector<uint8_t> plaintext(cipherLen + 32);
    int outLen = 0;
    EVP_DecryptUpdate(cipherCtx, plaintext.data(), &outLen,
                      ivAndCipher.data() + 16, cipherLen);
    int totalLen = outLen;
    if (EVP_DecryptFinal_ex(cipherCtx, plaintext.data() + outLen, &outLen) != 1) {
        EVP_CIPHER_CTX_free(cipherCtx);
        return "";
    }
    totalLen += outLen;
    EVP_CIPHER_CTX_free(cipherCtx);
    plaintext.resize(totalLen);

    return std::string(plaintext.begin(), plaintext.end());
}

// Zlib compress
std::vector<uint8_t> zlib_compress(const std::vector<uint8_t>& data) {
    if (data.empty()) return {};

    uLongf compressed_size = compressBound(data.size());
    std::vector<uint8_t> compressed(compressed_size);

    int result = compress2(compressed.data(), &compressed_size,
                           data.data(), data.size(), 6);
    if (result != Z_OK) return {};

    compressed.resize(compressed_size);
    return compressed;
}

// Zlib decompress
std::vector<uint8_t> zlib_decompress(const std::vector<uint8_t>& compressed_data) {
    if (compressed_data.empty()) return {};

    uLongf decompressed_size = compressed_data.size() * 4;
    std::vector<uint8_t> decompressed(decompressed_size);

    int result = uncompress(decompressed.data(), &decompressed_size,
                            compressed_data.data(), compressed_data.size());

    if (result == Z_BUF_ERROR) {
        decompressed_size *= 2;
        decompressed.resize(decompressed_size);
        result = uncompress(decompressed.data(), &decompressed_size,
                            compressed_data.data(), compressed_data.size());
    }

    if (result != Z_OK) return {};

    decompressed.resize(decompressed_size);
    return decompressed;
}

char* trim_newline(char* s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
    return s;
}

void set_field(char** field, const char* value) {
    if (*field) {
        free(*field);
    }
    *field = strdup(value ? value : "");
}

const char* get_version(void) {
    return "1.0.0";
}

int initialize_email_system(void) {
    if (system_initialized) {
        return 0;
    }
    LOG_INFO("[EMAIL_CORE] Initializing email system...\n");
    system_initialized = 1;
    return 0;
}

void shutdown_email_system(void) {
    if (!system_initialized) {
        return;
    }
    LOG_INFO("[EMAIL_CORE] Shutting down email system...\n");
    system_initialized = 0;
}

// Expose the global db handle for use by email_handler_c.cpp
extern "C" sqlite3* email_core_get_db() {
    return g_db;
}

// Expose the global db mutex for use by email_handler_c.cpp to prevent concurrent SQLite access
std::mutex& email_core_get_db_mutex() {
    return g_db_mutex;
}

// Helper: parse comma-separated recipient list
static std::vector<std::string> parse_recipients(const std::string& recipients) {
    std::vector<std::string> result;
    std::string current;
    for (char c : recipients) {
        if (c == ',' || c == ';') {
            // Trim whitespace
            size_t start = current.find_first_not_of(" \t\r\n");
            size_t end = current.find_last_not_of(" \t\r\n");
            if (start != std::string::npos) {
                result.push_back(current.substr(start, end - start + 1));
            }
            current.clear();
        } else {
            current += c;
        }
    }
    // Last one
    size_t start = current.find_first_not_of(" \t\r\n");
    size_t end = current.find_last_not_of(" \t\r\n");
    if (start != std::string::npos) {
        result.push_back(current.substr(start, end - start + 1));
    }
    return result;
}

// Helper: extract email address from "Name <email>" format
static std::string extract_email_addr(const std::string& input) {
    size_t lt = input.find('<');
    size_t gt = input.find('>');
    if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
        return input.substr(lt + 1, gt - lt - 1);
    }
    return input;
}

// Prepare encrypted data body for x_start_new=data messages
extern "C" int email_prepare_data_body(const char* plaintext, const char* recipients, const char* sender, char* outJson, int outSize) {
    if (!plaintext || !recipients || !outJson || outSize <= 0) return -1;

    std::string textStr(plaintext);
    std::string recipientsStr(recipients);
    std::string senderStr(sender ? sender : "");

    // Generate 12-char random password
    std::string aesPassword = generate_random_password(12);

    // Use SHA-256 of password as AES-256 key (32 bytes)
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    unsigned char aesKey[32];
    unsigned int hashLen = 0;
    EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(mdctx, aesPassword.data(), aesPassword.size());
    EVP_DigestFinal_ex(mdctx, aesKey, &hashLen);
    EVP_MD_CTX_free(mdctx);

    // AES-256-CBC encrypt the plaintext
    unsigned char iv[16];
    RAND_bytes(iv, 16);

    EVP_CIPHER_CTX* cipherCtx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(cipherCtx, EVP_aes_256_cbc(), NULL, aesKey, iv);

    std::vector<uint8_t> ciphertext(textStr.size() + 32);
    int outLen = 0;
    EVP_EncryptUpdate(cipherCtx, ciphertext.data(), &outLen,
                      (const unsigned char*)textStr.data(), textStr.size());
    int totalLen = outLen;
    EVP_EncryptFinal_ex(cipherCtx, ciphertext.data() + outLen, &outLen);
    totalLen += outLen;
    EVP_CIPHER_CTX_free(cipherCtx);
    ciphertext.resize(totalLen);

    // Prepend IV to ciphertext
    std::vector<uint8_t> ivAndCipher;
    ivAndCipher.insert(ivAndCipher.end(), iv, iv + 16);
    ivAndCipher.insert(ivAndCipher.end(), ciphertext.begin(), ciphertext.end());

    // Compress
    auto compressed = zlib_compress(ivAndCipher);
    if (compressed.empty()) {
        LOG_INFO("email_prepare_data_body: compression failed\n");
        return -2;
    }

    // Base64 encode
    std::string textB64 = base64_encode(compressed.data(), compressed.size());

    // Parse recipients and build code array
    auto recipientList = parse_recipients(recipientsStr);
    nlohmann::json codeArray = nlohmann::json::array();

    sqlite3* db = email_core_get_db();
    for (const auto& rawRecipient : recipientList) {
        std::string acct = extract_email_addr(rawRecipient);
        if (acct.empty()) continue;

        // Look up pubkey from code table
        std::string pubPem;
        if (db) {
            const char* sql = "SELECT pubkey FROM code WHERE account = ? ORDER BY id DESC LIMIT 1;";
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, acct.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* pub = (const char*)sqlite3_column_text(stmt, 0);
                    if (pub) pubPem = pub;
                }
                sqlite3_finalize(stmt);
            }
        }

        if (pubPem.empty()) {
            LOG_INFO("email_prepare_data_body: no pubkey for account=%s, skipping\n", acct.c_str());
            continue;
        }

        // Encrypt the AES password with recipient's public key
        std::string encryptedPwd = ecc_encrypt_with_public_key(pubPem, aesPassword);
        if (encryptedPwd.empty()) {
            LOG_INFO("email_prepare_data_body: ECC encrypt failed for account=%s\n", acct.c_str());
            continue;
        }

        std::string identify = compute_md5(pubPem);

        nlohmann::json codeEntry;
        codeEntry["account"] = acct;
        codeEntry["password"] = encryptedPwd;
        codeEntry["md5"] = identify;
        codeArray.push_back(codeEntry);
    }

    if (codeArray.empty()) {
        LOG_INFO("email_prepare_data_body: no valid recipients with pubkeys\n");
        return -3;
    }

    // Build the encrypted body JSON
    nlohmann::json bodyJson;
    bodyJson["text"] = textB64;
    bodyJson["session_info"] = {{"code", codeArray}};

    std::string result = bodyJson.dump();
    if ((int)result.size() >= outSize) {
        LOG_INFO("email_prepare_data_body: output too large (%zu >= %d)\n", result.size(), outSize);
        return -4;
    }
    snprintf(outJson, outSize, "%s", result.c_str());

    LOG_INFO("email_prepare_data_body: success, text_len=%zu, code_entries=%zu\n",
             textB64.size(), codeArray.size());
    return 0;
}

// Decrypt data body for x_start_new=data messages
extern "C" int email_decrypt_data_body(const char* encryptedBody, const char* account, char* outJson, int outSize) {
    if (!encryptedBody || !account || !outJson || outSize <= 0) return -1;

    try {
        auto bodyJson = nlohmann::json::parse(encryptedBody);
        std::string textB64 = bodyJson.value("text", "");
        auto sessionInfo = bodyJson.value("session_info", nlohmann::json::object());
        auto codeArray = sessionInfo.value("code", nlohmann::json::array());

        if (textB64.empty() || codeArray.empty()) {
            LOG_INFO("email_decrypt_data_body: missing text or code array\n");
            return -2;
        }

        // Find own account in code array and get encrypted password
        std::string encryptedPwd;
        std::string expectedMd5;
        for (const auto& entry : codeArray) {
            // Each entry: {"account": "...", "password": "...", "md5": "..."}
            std::string entryAccount = entry.value("account", "");
            if (entryAccount == account) {
                encryptedPwd = entry.value("password", "");
                expectedMd5 = entry.value("md5", "");
                break;
            }
        }

        if (encryptedPwd.empty()) {
            LOG_INFO("email_decrypt_data_body: account %s not found in code array\n", account);
            return -3;
        }

        // Look up own private key from keyinfo table using account and md5 of pubkey
        std::string privPem, keyPassword;
        sqlite3* db = email_core_get_db();
        if (db) {
            sqlite3_stmt* stmt;
            const char* sql = "SELECT key, password, pub FROM keyinfo WHERE account = ? AND key != '' ORDER BY id DESC;";
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, account, -1, SQLITE_TRANSIENT);
                LOG_INFO("email_decrypt_data_body: looking up keyinfo for account=%s, expectedMd5=%s\n", account, expectedMd5.c_str());
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* sk = (const char*)sqlite3_column_text(stmt, 0);
                    const char* kp = (const char*)sqlite3_column_text(stmt, 1);
                    const char* pub = (const char*)sqlite3_column_text(stmt, 2);
                    std::string pubMd5 = pub ? compute_md5(std::string(pub)) : "";
                    LOG_INFO("email_decrypt_data_body: keyinfo row, pubMd5=%s, expectedMd5=%s, match=%d\n", pubMd5.c_str(), expectedMd5.c_str(), pubMd5 == expectedMd5 ? 1 : 0);
                    if (pub && pubMd5 == expectedMd5) {
                        if (sk) privPem = sk;
                        if (kp) keyPassword = kp;
                        break;
                    }
                }
                sqlite3_finalize(stmt);
            } else {
                LOG_INFO("email_decrypt_data_body: keyinfo prepare failed: %s\n", sqlite3_errmsg(db));
            }
        }

        if (privPem.empty()) {
            LOG_INFO("email_decrypt_data_body: no private key for account=%s\n", account);
            return -4;
        }

        // Decrypt the AES password using ECC private key
        std::string aesPassword = ecc_decrypt_with_private_key(privPem, keyPassword, encryptedPwd);
        if (aesPassword.empty()) {
            LOG_INFO("email_decrypt_data_body: ECC decrypt failed for account=%s\n", account);
            return -5;
        }

        // Derive AES key from password (SHA-256)
        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        unsigned char aesKey[32];
        unsigned int hashLen = 0;
        EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
        EVP_DigestUpdate(mdctx, aesPassword.data(), aesPassword.size());
        EVP_DigestFinal_ex(mdctx, aesKey, &hashLen);
        EVP_MD_CTX_free(mdctx);

        // Base64 decode the text
        auto compressed = base64_decode(textB64);
        if (compressed.empty()) {
            LOG_INFO("email_decrypt_data_body: base64 decode failed\n");
            return -6;
        }

        // Decompress
        auto ivAndCipher = zlib_decompress(compressed);
        if (ivAndCipher.empty()) {
            LOG_INFO("email_decrypt_data_body: decompress failed\n");
            return -7;
        }

        if (ivAndCipher.size() < 16) {
            LOG_INFO("email_decrypt_data_body: data too short after decompress\n");
            return -8;
        }

        // Extract IV and ciphertext
        unsigned char iv[16];
        memcpy(iv, ivAndCipher.data(), 16);
        int cipherLen = (int)ivAndCipher.size() - 16;

        // AES-256-CBC decrypt
        EVP_CIPHER_CTX* cipherCtx = EVP_CIPHER_CTX_new();
        EVP_DecryptInit_ex(cipherCtx, EVP_aes_256_cbc(), NULL, aesKey, iv);

        std::vector<uint8_t> plaintext(cipherLen + 32);
        int outLen = 0;
        EVP_DecryptUpdate(cipherCtx, plaintext.data(), &outLen,
                          ivAndCipher.data() + 16, cipherLen);
        int totalLen = outLen;
        if (EVP_DecryptFinal_ex(cipherCtx, plaintext.data() + outLen, &outLen) != 1) {
            EVP_CIPHER_CTX_free(cipherCtx);
            LOG_INFO("email_decrypt_data_body: AES decrypt finalization failed\n");
            return -9;
        }
        totalLen += outLen;
        EVP_CIPHER_CTX_free(cipherCtx);
        plaintext.resize(totalLen);

        std::string result(plaintext.begin(), plaintext.end());
        if ((int)result.size() >= outSize) {
            LOG_INFO("email_decrypt_data_body: output too large\n");
            return -10;
        }
        snprintf(outJson, outSize, "%s", result.c_str());

        LOG_INFO("email_decrypt_data_body: success, plaintext_len=%zu\n", result.size());
        return 0;

    } catch (const std::exception& e) {
        LOG_INFO("email_decrypt_data_body: exception: %s\n", e.what());
        return -11;
    }
}
