#ifndef EMAIL_CORE_COMMON_H
#define EMAIL_CORE_COMMON_H

#include <sqlite3.h>
#include <mutex>
#include <string>
#include <vector>

// Shared global database handle and mutex
extern sqlite3* g_db;
extern std::mutex g_db_mutex;

// Utility functions shared across modules
std::string generate_email_id(const char* type);
bool generate_ecc_keypair(std::string& outPubPem, std::string& outPrivPem, const std::string& password);
std::string generate_random_password(int length);
std::string sign_with_ecc_private_key(const std::string& privPem, const std::string& keyPassword, const std::string& data);
bool verify_with_ecc_public_key(const std::string& pubPem, const std::string& data, const std::string& signatureB64);
std::string base64_encode(const unsigned char* data, size_t len);
std::vector<uint8_t> base64_decode(const std::string& encoded);

// Wire-format body wrapping (PGP-inline style): notice text + base64 armor so
// protocol JSON is not readable as JSON in external mail clients. For bodies
// containing a large already-base64 string field (e.g. chunk ciphertext), that
// field is split into a second "OIM DATA" block to avoid double base64 encoding.
std::string oim_wrap_body(const std::string& body);
std::string oim_unwrap_body(const std::string& text); // passthrough when no markers
std::string compute_md5(const std::string& input);
std::string ecc_encrypt_with_public_key(const std::string& pubPem, const std::string& plaintext);
std::string ecc_decrypt_with_private_key(const std::string& privPem, const std::string& keyPassword, const std::string& ciphertext);
std::vector<uint8_t> zlib_compress(const std::vector<uint8_t>& data);
std::vector<uint8_t> zlib_decompress(const std::vector<uint8_t>& compressed);
char* trim_newline(char* s);
void set_field(char** field, const char* value);

// DB access helpers
extern "C" sqlite3* email_core_get_db();
std::mutex& email_core_get_db_mutex();

#endif // EMAIL_CORE_COMMON_H
