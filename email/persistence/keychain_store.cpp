#include "keychain_store.h"
#include "logger.h"
#include <string.h>
#include <stdint.h>
#include <mutex>

#if defined(__APPLE__) || defined(__ANDROID__)
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <fstream>
#include <filesystem>
#include <vector>
#endif
#if defined(__APPLE__)
#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include <wincred.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <vector>
#endif

namespace keychain_store {

static const char* kServiceDb = "com.redsalmon.oim.db";
static const char* kServiceCreds = "com.redsalmon.oim.credentials";
static const char* kAccountMaster = "master";

static std::string hex_encode(const uint8_t* p, size_t n) {
    static const char* t = "0123456789abcdef";
    std::string out(n * 2, '0');
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = t[p[i] >> 4];
        out[2 * i + 1] = t[p[i] & 0xf];
    }
    return out;
}

#if defined(__APPLE__)
// ---------------------------------------------------------------------------
// macOS: Security.framework generic-password items
// ---------------------------------------------------------------------------

static CFDataRef copy_query_item(const char* service, const std::string& account,
                                 bool noUi = false) {
    CFStringRef svc = CFStringCreateWithCString(nullptr, service, kCFStringEncodingUTF8);
    CFStringRef acc = CFStringCreateWithCString(nullptr, account.c_str(), kCFStringEncodingUTF8);
    const void* keys[] = {
        kSecClass, kSecAttrService, kSecAttrAccount,
        kSecReturnData, kSecMatchLimit, kSecUseAuthenticationUI,
    };
    const void* vals[] = {
        kSecClassGenericPassword, svc, acc,
        kCFBooleanTrue, kSecMatchLimitOne,
        noUi ? kSecUseAuthenticationUISkip : kSecUseAuthenticationUIAllow,
    };
    CFDictionaryRef q = CFDictionaryCreate(nullptr, keys, vals, 6,
                                           &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    CFRelease(svc);
    CFRelease(acc);
    CFDataRef data = nullptr;
    OSStatus rc = SecItemCopyMatching(q, (CFTypeRef*)&data);
    if (rc != errSecSuccess && rc != errSecItemNotFound) {
        LOG_INFO("[keychain] SecItemCopyMatching rc=%d service=%s\n", (int)rc, service);
    }
    CFRelease(q);
    return data;
}

static bool upsert_item(const char* service, const std::string& account,
                        const void* bytes, size_t len) {
    CFStringRef svc = CFStringCreateWithCString(nullptr, service, kCFStringEncodingUTF8);
    CFStringRef acc = CFStringCreateWithCString(nullptr, account.c_str(), kCFStringEncodingUTF8);

    const void* qKeys[] = { kSecClass, kSecAttrService, kSecAttrAccount };
    const void* qVals[] = { kSecClassGenericPassword, svc, acc };
    CFDictionaryRef q = CFDictionaryCreate(nullptr, qKeys, qVals, 3,
                                           &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);

    CFDataRef data = CFDataCreate(nullptr, (const UInt8*)bytes, (CFIndex)len);
    const void* aKeys[] = { kSecValueData };
    const void* aVals[] = { data };
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr, aKeys, aVals, 1,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);

    OSStatus rc = SecItemUpdate(q, attrs);
    if (rc == errSecItemNotFound) {
        // New item: add attributes + data + ThisDeviceOnly accessibility.
        const void* iKeys[] = {
            kSecClass, kSecAttrService, kSecAttrAccount,
            kSecValueData, kSecAttrAccessible,
        };
        const void* iVals[] = {
            kSecClassGenericPassword, svc, acc,
            data, kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
        };
        CFDictionaryRef item = CFDictionaryCreate(nullptr, iKeys, iVals, 5,
                                                  &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
        rc = SecItemAdd(item, nullptr);
        CFRelease(item);
    }
    if (rc != errSecSuccess) {
        LOG_INFO("[keychain] SecItemUpdate/Add rc=%d service=%s\n", (int)rc, service);
    }

    CFRelease(attrs);
    CFRelease(data);
    CFRelease(q);
    CFRelease(svc);
    CFRelease(acc);
    return rc == errSecSuccess;
}

static bool delete_item(const char* service, const std::string& account,
                        bool noUi = false) {
    CFStringRef svc = CFStringCreateWithCString(nullptr, service, kCFStringEncodingUTF8);
    CFStringRef acc = CFStringCreateWithCString(nullptr, account.c_str(), kCFStringEncodingUTF8);
    const void* keys[] = { kSecClass, kSecAttrService, kSecAttrAccount,
                           kSecUseAuthenticationUI };
    const void* vals[] = { kSecClassGenericPassword, svc, acc,
                           noUi ? kSecUseAuthenticationUISkip : kSecUseAuthenticationUIAllow };
    CFDictionaryRef q = CFDictionaryCreate(nullptr, keys, vals, 4,
                                           &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    OSStatus rc = SecItemDelete(q);
    CFRelease(q);
    CFRelease(svc);
    CFRelease(acc);
    return rc == errSecSuccess || rc == errSecItemNotFound;
}

std::string db_master_key_hex() {
    static std::string cached;
    if (!cached.empty()) return cached;

    CFDataRef existing = copy_query_item(kServiceDb, kAccountMaster);
    if (existing) {
        cached.assign((const char*)CFDataGetBytePtr(existing),
                      (size_t)CFDataGetLength(existing));
        CFRelease(existing);
        return cached;
    }

    uint8_t key[32];
    if (SecRandomCopyBytes(kSecRandomDefault, sizeof(key), key) != errSecSuccess) {
        LOG_INFO("[keychain] SecRandomCopyBytes failed\n");
        return "";
    }
    std::string hex = hex_encode(key, sizeof(key));
    memset(key, 0, sizeof(key));

    if (!upsert_item(kServiceDb, kAccountMaster, hex.data(), hex.size())) {
        LOG_INFO("[keychain] failed to persist db master key\n");
        return "";
    }
    cached = hex;
    return cached;
}

#elif defined(__ANDROID__)
// ---------------------------------------------------------------------------
// Android: the master key is generated and wrapped with AndroidKeyStore on
// the Kotlin side; the plaintext hex is injected once per process before any
// database access. Nothing is persisted from C++.
// ---------------------------------------------------------------------------

static std::string g_injectedMasterKey;

void inject_db_master_key_hex(const char* hex) {
    g_injectedMasterKey = hex ? hex : "";
}

std::string db_master_key_hex() {
    return g_injectedMasterKey;
}

#endif

#if defined(__APPLE__) || defined(__ANDROID__)
// ---------------------------------------------------------------------------
// Credential vault: account secrets live in a single AES-256-GCM encrypted
// file, keyed by the same master key as the database. Only the master key
// touches the platform secret store (Keychain on macOS, AndroidKeyStore via
// Kotlin on Android).
// Layout: magic(4) | iv(12) | ciphertext | tag(16); plaintext is a JSON map.
// ---------------------------------------------------------------------------

static std::string g_vaultPath;
static std::mutex g_vaultMutex;

void set_vault_path(const std::string& path) {
    std::lock_guard<std::mutex> lk(g_vaultMutex);
    g_vaultPath = path;
}

static bool hex_decode(const std::string& hex, uint8_t* out, size_t n) {
    if (hex.size() != n * 2) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < n; i++) {
        int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool vault_master_key(uint8_t out[32]) {
    std::string hex = db_master_key_hex();
    return !hex.empty() && hex_decode(hex, out, 32);
}

static bool gcm_encrypt(const uint8_t key[32], const std::string& plain,
                        std::vector<uint8_t>& out) {
    uint8_t iv[12];
    if (RAND_bytes(iv, sizeof(iv)) != 1) return false;
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    if (!c) return false;
    bool ok = EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, sizeof(iv), nullptr) == 1 &&
              EVP_EncryptInit_ex(c, nullptr, nullptr, key, iv) == 1;
    out.resize(sizeof(iv) + plain.size() + 16);
    memcpy(out.data(), iv, sizeof(iv));
    int len = 0, total = 0;
    if (ok) ok = EVP_EncryptUpdate(c, out.data() + sizeof(iv), &len,
                                   (const uint8_t*)plain.data(), (int)plain.size()) == 1;
    total += ok ? len : 0;
    if (ok) ok = EVP_EncryptFinal_ex(c, out.data() + sizeof(iv) + total, &len) == 1;
    total += ok ? len : 0;
    if (ok) ok = EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 16,
                                     out.data() + sizeof(iv) + total) == 1;
    EVP_CIPHER_CTX_free(c);
    if (!ok) return false;
    out.resize(sizeof(iv) + total + 16);
    return true;
}

static bool gcm_decrypt(const uint8_t key[32], const uint8_t* blob, size_t n,
                        std::string& plain) {
    if (n < 12 + 16) return false;
    const uint8_t* iv = blob;
    size_t ctLen = n - 12 - 16;
    const uint8_t* tag = blob + 12 + ctLen;
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    if (!c) return false;
    bool ok = EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1 &&
              EVP_DecryptInit_ex(c, nullptr, nullptr, key, iv) == 1;
    std::vector<uint8_t> buf(ctLen ? ctLen : 1);
    int len = 0, total = 0;
    if (ok && ctLen) ok = EVP_DecryptUpdate(c, buf.data(), &len, blob + 12, (int)ctLen) == 1;
    total += ok ? len : 0;
    if (ok) ok = EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, (void*)tag) == 1;
    if (ok) ok = EVP_DecryptFinal_ex(c, buf.data() + total, &len) > 0;
    total += ok ? len : 0;
    EVP_CIPHER_CTX_free(c);
    if (!ok) return false;
    plain.assign((const char*)buf.data(), total);
    return true;
}

// Caller must hold g_vaultMutex.
static bool vault_read(nlohmann::json& map) {
    map = nlohmann::json::object();
    if (g_vaultPath.empty()) return false;
    std::ifstream f(g_vaultPath, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    static const char kMagic[4] = {'O', 'T', 'V', '1'};
    if (blob.size() < 4 + 12 + 16 || memcmp(blob.data(), kMagic, 4) != 0) return false;
    uint8_t key[32];
    if (!vault_master_key(key)) return false;
    std::string plain;
    bool ok = gcm_decrypt(key, blob.data() + 4, blob.size() - 4, plain);
    memset(key, 0, sizeof(key));
    if (!ok) return false;
    try {
        map = nlohmann::json::parse(plain);
    } catch (...) {
        return false;
    }
    return map.is_object();
}

// Caller must hold g_vaultMutex.
static bool vault_write(const nlohmann::json& map) {
    if (g_vaultPath.empty()) return false;
    uint8_t key[32];
    if (!vault_master_key(key)) return false;
    std::vector<uint8_t> blob;
    bool ok = gcm_encrypt(key, map.dump(), blob);
    memset(key, 0, sizeof(key));
    if (!ok) return false;
    static const char kMagic[4] = {'O', 'T', 'V', '1'};
    blob.insert(blob.begin(), kMagic, kMagic + 4);

    std::filesystem::path tmp = g_vaultPath + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write((const char*)blob.data(), (std::streamsize)blob.size());
        if (!f.good()) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, g_vaultPath, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        LOG_INFO("[keychain] vault rename failed: %s\n", ec.message().c_str());
        return false;
    }
    return true;
}

bool store_secret(const std::string& account, const std::string& secret) {
    if (account.empty()) return false;
    std::lock_guard<std::mutex> lk(g_vaultMutex);
    nlohmann::json map;
    vault_read(map); // missing/corrupt file starts fresh
    map[account] = secret;
    return vault_write(map);
}

std::string load_secret(const std::string& account) {
    if (account.empty()) return "";
    std::lock_guard<std::mutex> lk(g_vaultMutex);
    nlohmann::json map;
    if (vault_read(map)) {
        auto it = map.find(account);
        if (it != map.end() && it->is_string()) return it->get<std::string>();
    }
#if defined(__APPLE__)
    // One-time migration: secrets written before the vault existed live as
    // individual Keychain items. Read without UI: items whose ACL already
    // trusts this signer migrate silently; anything that would prompt is
    // skipped so the user never gets a cascade of auth dialogs.
    CFDataRef legacy = copy_query_item(kServiceCreds, account, /*noUi=*/true);
    if (!legacy) return "";
    std::string secret((const char*)CFDataGetBytePtr(legacy), (size_t)CFDataGetLength(legacy));
    CFRelease(legacy);
    map[account] = secret;
    if (vault_write(map)) {
        delete_item(kServiceCreds, account, /*noUi=*/true);
        LOG_INFO("[keychain] migrated credential for %s into vault\n", account.c_str());
    }
    return secret;
#else
    return "";
#endif
}

bool delete_secret(const std::string& account) {
    if (account.empty()) return false;
    std::lock_guard<std::mutex> lk(g_vaultMutex);
    nlohmann::json map;
    bool ok = true;
    if (vault_read(map) && map.erase(account) > 0) {
        ok = vault_write(map);
    }
#if defined(__APPLE__)
    delete_item(kServiceCreds, account, /*noUi=*/true); // drop any pre-vault remnant too
#endif
    return ok;
}

#elif defined(_WIN32)
// ---------------------------------------------------------------------------
// Windows: Credential Manager generic credentials (advapi32 CredRead/Write/
// Delete), one item per (service, account). The vault is DPAPI-protected and
// bound to the current logon session — like the ThisDeviceOnly Keychain items
// it is only readable by this user on this machine, with no unlock prompt.
// TargetName: "OceanTalk:<service>:<account>". Persist is LOCAL_MACHINE so
// credentials never roam — equivalent to ThisDeviceOnly.
//
// Legacy DPAPI blob files under %LOCALAPPDATA%\OceanTalk\keychain are read as
// a fallback and migrated into the vault on first access.
// ---------------------------------------------------------------------------

static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::wstring target_name(const char* service, const std::string& account) {
    return utf8_to_wide(std::string("OceanTalk:") + service + ":" + account);
}

// --- Credential Manager primitives ---

static bool cred_read(const char* service, const std::string& account, std::string& out) {
    std::wstring tn = target_name(service, account);
    PCREDENTIALW c = nullptr;
    if (!CredReadW(tn.c_str(), CRED_TYPE_GENERIC, 0, &c)) return false;
    out.assign((const char*)c->CredentialBlob, c->CredentialBlobSize);
    CredFree(c);
    return true;
}

static bool cred_write(const char* service, const std::string& account,
                       const void* bytes, size_t len) {
    if (len == 0 || len > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        LOG_INFO("[keychain] CredWrite rejected len=%zu service=%s\n", len, service);
        return false;
    }
    std::wstring tn = target_name(service, account);
    std::wstring un = utf8_to_wide(account);
    CREDENTIALW c = {};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = (LPWSTR)tn.c_str();
    c.CredentialBlob = (LPBYTE)bytes;
    c.CredentialBlobSize = (DWORD)len;
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    c.UserName = (LPWSTR)un.c_str();
    if (!CredWriteW(&c, 0)) {
        LOG_INFO("[keychain] CredWrite failed err=%lu service=%s\n", GetLastError(), service);
        return false;
    }
    return true;
}

static bool cred_delete(const char* service, const std::string& account) {
    std::wstring tn = target_name(service, account);
    if (CredDeleteW(tn.c_str(), CRED_TYPE_GENERIC, 0)) return true;
    return GetLastError() == ERROR_NOT_FOUND;
}

// --- Legacy DPAPI blob files (migration path only) ---
// Layout: %LOCALAPPDATA%\OceanTalk\keychain\<service>\<hex(account)>.bin

static std::filesystem::path keychain_root() {
    static std::filesystem::path root;
    if (!root.empty()) return root;
    PWSTR p = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &p) == S_OK) {
        root = std::filesystem::path(p) / L"OceanTalk" / L"keychain";
        CoTaskMemFree(p);
    } else {
        root = std::filesystem::temp_directory_path() / L"OceanTalk" / L"keychain";
    }
    return root;
}

static std::filesystem::path item_path(const char* service, const std::string& account) {
    return keychain_root() / service /
           (hex_encode((const uint8_t*)account.data(), account.size()) + ".bin");
}

static bool legacy_read_item(const char* service, const std::string& account, std::string& out) {
    std::ifstream f(item_path(service, account), std::ios::binary);
    if (!f) return false;
    std::vector<char> enc((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (enc.empty()) return false;

    DATA_BLOB in;
    in.pbData = (BYTE*)enc.data();
    in.cbData = (DWORD)enc.size();
    DATA_BLOB entropy;
    entropy.pbData = (BYTE*)service;
    entropy.cbData = (DWORD)strlen(service);
    DATA_BLOB dec = {};
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &dec)) {
        LOG_INFO("[keychain] legacy CryptUnprotectData failed err=%lu service=%s\n",
                 GetLastError(), service);
        return false;
    }
    out.assign((const char*)dec.pbData, dec.cbData);
    SecureZeroMemory(dec.pbData, dec.cbData);
    LocalFree(dec.pbData);
    return true;
}

static void legacy_delete_item(const char* service, const std::string& account) {
    std::error_code ec;
    std::filesystem::remove(item_path(service, account), ec);
}

// --- item API: vault first, legacy file fallback with migration ---

static bool read_item(const char* service, const std::string& account, std::string& out) {
    if (cred_read(service, account, out)) return true;
    // One-time migration: a legacy DPAPI file is moved into the vault.
    if (!legacy_read_item(service, account, out)) return false;
    if (cred_write(service, account, out.data(), out.size())) {
        legacy_delete_item(service, account);
        LOG_INFO("[keychain] migrated %s/%s to Credential Manager\n", service, account.c_str());
    }
    return true;
}

static bool upsert_item(const char* service, const std::string& account,
                        const void* bytes, size_t len) {
    if (!cred_write(service, account, bytes, len)) return false;
    legacy_delete_item(service, account);  // drop any legacy copy
    return true;
}

static bool delete_item(const char* service, const std::string& account) {
    legacy_delete_item(service, account);
    return cred_delete(service, account);
}

std::string db_master_key_hex() {
    static std::string cached;
    if (!cached.empty()) return cached;

    if (read_item(kServiceDb, kAccountMaster, cached) && !cached.empty()) {
        return cached;
    }

    uint8_t key[32];
    NTSTATUS st = BCryptGenRandom(nullptr, key, sizeof(key), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (st != 0) {
        LOG_INFO("[keychain] BCryptGenRandom failed st=0x%08lx\n", (unsigned long)st);
        return "";
    }
    std::string hex = hex_encode(key, sizeof(key));
    SecureZeroMemory(key, sizeof(key));

    if (!upsert_item(kServiceDb, kAccountMaster, hex.data(), hex.size())) {
        LOG_INFO("[keychain] failed to persist db master key\n");
        return "";
    }
    cached = hex;
    return cached;
}

bool store_secret(const std::string& account, const std::string& secret) {
    if (account.empty()) return false;
    return upsert_item(kServiceCreds, account, secret.data(), secret.size());
}

std::string load_secret(const std::string& account) {
    if (account.empty()) return "";
    std::string out;
    if (!read_item(kServiceCreds, account, out)) return "";
    return out;
}

bool delete_secret(const std::string& account) {
    if (account.empty()) return false;
    return delete_item(kServiceCreds, account);
}

// Windows stores each secret as its own DPAPI blob; no vault file needed.
void set_vault_path(const std::string&) {}

#else
#error "keychain_store: no secret-storage backend for this platform"
#endif

} // namespace keychain_store
