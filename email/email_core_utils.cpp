#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include "email_handler_c.h"
#include "db_connection.h"
#include "key_repo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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

// ---------------------------------------------------------------------------
// Wire format: notice text part + single-entry zip attachment ("message.oim")
// carrying the protocol JSON. The zip entry uses raw deflate.
// ---------------------------------------------------------------------------

const char* oim_notice_text() {
    return "这是一条私密消息，请在零海客户端中查看。\n"
           "This is an OceanTalk-encrypted message — open it in the OceanTalk client.\n";
}

static void oim_put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x & 0xff));
    v.push_back((uint8_t)((x >> 8) & 0xff));
}
static void oim_put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; i++) v.push_back((uint8_t)((x >> (8 * i)) & 0xff));
}

std::vector<uint8_t> oim_zip_pack(const std::string& text) {
    std::vector<uint8_t> def(compressBound((uLong)text.size()));
    z_stream zs = {};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return {};
    }
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(text.data()));
    zs.avail_in = (uInt)text.size();
    zs.next_out = def.data();
    zs.avail_out = (uInt)def.size();
    int rc = deflate(&zs, Z_FINISH);
    if (rc != Z_STREAM_END) { deflateEnd(&zs); return {}; }
    def.resize(zs.total_out);
    deflateEnd(&zs);

    static const char name[] = "message.json";
    const uint16_t nameLen = (uint16_t)(sizeof(name) - 1);
    const uint32_t crc = (uint32_t)crc32(0, reinterpret_cast<const Bytef*>(text.data()), (uInt)text.size());
    const uint32_t compSize = (uint32_t)def.size();
    const uint32_t rawSize = (uint32_t)text.size();

    std::vector<uint8_t> z;
    z.reserve(def.size() + 128);
    // Local file header
    oim_put32(z, 0x04034b50); oim_put16(z, 20); oim_put16(z, 0); oim_put16(z, 8);
    oim_put16(z, 0); oim_put16(z, 0);
    oim_put32(z, crc); oim_put32(z, compSize); oim_put32(z, rawSize);
    oim_put16(z, nameLen); oim_put16(z, 0);
    z.insert(z.end(), name, name + nameLen);
    z.insert(z.end(), def.begin(), def.end());
    // Central directory entry
    const uint32_t cdOff = (uint32_t)z.size();
    oim_put32(z, 0x02014b50); oim_put16(z, 20); oim_put16(z, 20); oim_put16(z, 0); oim_put16(z, 8);
    oim_put16(z, 0); oim_put16(z, 0);
    oim_put32(z, crc); oim_put32(z, compSize); oim_put32(z, rawSize);
    oim_put16(z, nameLen); oim_put16(z, 0); oim_put16(z, 0); oim_put16(z, 0); oim_put16(z, 0);
    oim_put32(z, 0); oim_put32(z, 0);
    z.insert(z.end(), name, name + nameLen);
    const uint32_t cdSize = (uint32_t)z.size() - cdOff;
    // End of central directory
    oim_put32(z, 0x06054b50); oim_put16(z, 0); oim_put16(z, 0); oim_put16(z, 1); oim_put16(z, 1);
    oim_put32(z, cdSize); oim_put32(z, cdOff); oim_put16(z, 0);
    return z;
}

std::string oim_zip_unpack(const std::vector<uint8_t>& zipData) {
    if (zipData.size() < 30) return {};
    auto le16 = [&](size_t o) { return (uint16_t)(zipData[o] | (zipData[o + 1] << 8)); };
    auto le32 = [&](size_t o) {
        return (uint32_t)zipData[o] | ((uint32_t)zipData[o + 1] << 8) |
               ((uint32_t)zipData[o + 2] << 16) | ((uint32_t)zipData[o + 3] << 24);
    };
    if (le32(0) != 0x04034b50) return {};
    const uint16_t method = le16(8);
    const uint32_t compSize = le32(18);
    const uint32_t rawSize = le32(22);
    const size_t dataOff = 30 + le16(26) + le16(28);
    if (dataOff + compSize > zipData.size()) return {};
    if (method == 0) {
        return std::string(reinterpret_cast<const char*>(zipData.data() + dataOff), compSize);
    }
    if (method != 8 || rawSize == 0 || rawSize > 256u * 1024 * 1024) return {};
    std::string out(rawSize, '\0');
    z_stream zs = {};
    if (inflateInit2(&zs, -15) != Z_OK) return {};
    zs.next_in = const_cast<Bytef*>(zipData.data() + dataOff);
    zs.avail_in = compSize;
    zs.next_out = reinterpret_cast<Bytef*>(&out[0]);
    zs.avail_out = rawSize;
    int rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (rc != Z_STREAM_END) return {};
    out.resize(zs.total_out);
    return out;
}

// Verify signature with ECC public key
bool verify_with_ecc_public_key(const std::string& pubPem, const std::string& data, const std::string& signatureB64) {
    BIO* bio = BIO_new_mem_buf(pubPem.data(), (int)pubPem.size());
    if (!bio) return false;

    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!pkey) {
        LOG_INFO("verify_with_ecc_public_key: failed to load public key\n");
        return false;
    }

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        EVP_PKEY_free(pkey);
        return false;
    }

    if (EVP_DigestVerifyInit(mdctx, NULL, EVP_sha256(), NULL, pkey) != 1) {
        LOG_INFO("verify_with_ecc_public_key: DigestVerifyInit failed\n");
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return false;
    }

    if (EVP_DigestVerifyUpdate(mdctx, data.data(), data.size()) != 1) {
        LOG_INFO("verify_with_ecc_public_key: DigestVerifyUpdate failed\n");
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return false;
    }

    std::vector<uint8_t> sig = base64_decode(signatureB64);
    int ret = EVP_DigestVerifyFinal(mdctx, sig.data(), sig.size());
    
    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);

    if (ret == 1) {
        return true;
    } else {
        LOG_INFO("verify_with_ecc_public_key: DigestVerifyFinal failed or invalid signature\n");
        return false;
    }
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

