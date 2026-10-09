#include "keychain_store.h"
#include "logger.h"
#include <string.h>
#include <stdint.h>

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

static CFDataRef copy_query_item(const char* service, const std::string& account) {
    CFStringRef svc = CFStringCreateWithCString(nullptr, service, kCFStringEncodingUTF8);
    CFStringRef acc = CFStringCreateWithCString(nullptr, account.c_str(), kCFStringEncodingUTF8);
    const void* keys[] = {
        kSecClass, kSecAttrService, kSecAttrAccount,
        kSecReturnData, kSecMatchLimit,
    };
    const void* vals[] = {
        kSecClassGenericPassword, svc, acc,
        kCFBooleanTrue, kSecMatchLimitOne,
    };
    CFDictionaryRef q = CFDictionaryCreate(nullptr, keys, vals, 5,
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

static bool delete_item(const char* service, const std::string& account) {
    CFStringRef svc = CFStringCreateWithCString(nullptr, service, kCFStringEncodingUTF8);
    CFStringRef acc = CFStringCreateWithCString(nullptr, account.c_str(), kCFStringEncodingUTF8);
    const void* keys[] = { kSecClass, kSecAttrService, kSecAttrAccount };
    const void* vals[] = { kSecClassGenericPassword, svc, acc };
    CFDictionaryRef q = CFDictionaryCreate(nullptr, keys, vals, 3,
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

bool store_secret(const std::string& account, const std::string& secret) {
    if (account.empty()) return false;
    return upsert_item(kServiceCreds, account, secret.data(), secret.size());
}

std::string load_secret(const std::string& account) {
    if (account.empty()) return "";
    CFDataRef data = copy_query_item(kServiceCreds, account);
    if (!data) return "";
    std::string out((const char*)CFDataGetBytePtr(data), (size_t)CFDataGetLength(data));
    CFRelease(data);
    return out;
}

bool delete_secret(const std::string& account) {
    if (account.empty()) return false;
    return delete_item(kServiceCreds, account);
}

#elif defined(_WIN32)
// ---------------------------------------------------------------------------
// Windows: DPAPI (CryptProtectData) blobs, one file per (service, account).
// DPAPI keys are derived from the user's logon credentials, so like the
// ThisDeviceOnly Keychain items the blobs are only readable by this user on
// this machine. Service name is mixed in as optional entropy so a blob from
// one service can't be replayed as another.
// Layout: %LOCALAPPDATA%\OceanTalk\keychain\<service>\<hex(account)>.bin
// ---------------------------------------------------------------------------

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

static DATA_BLOB entropy_for(const char* service) {
    DATA_BLOB e;
    e.pbData = (BYTE*)service;
    e.cbData = (DWORD)strlen(service);
    return e;
}

static bool read_item(const char* service, const std::string& account, std::string& out) {
    std::ifstream f(item_path(service, account), std::ios::binary);
    if (!f) return false;
    std::vector<char> enc((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (enc.empty()) return false;

    DATA_BLOB in;
    in.pbData = (BYTE*)enc.data();
    in.cbData = (DWORD)enc.size();
    DATA_BLOB entropy = entropy_for(service);
    DATA_BLOB dec = {};
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &dec)) {
        LOG_INFO("[keychain] CryptUnprotectData failed err=%lu service=%s\n",
                 GetLastError(), service);
        return false;
    }
    out.assign((const char*)dec.pbData, dec.cbData);
    SecureZeroMemory(dec.pbData, dec.cbData);
    LocalFree(dec.pbData);
    return true;
}

static bool upsert_item(const char* service, const std::string& account,
                        const void* bytes, size_t len) {
    DATA_BLOB in;
    in.pbData = (BYTE*)bytes;
    in.cbData = (DWORD)len;
    DATA_BLOB entropy = entropy_for(service);
    DATA_BLOB enc = {};
    if (!CryptProtectData(&in, L"OceanTalk", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &enc)) {
        LOG_INFO("[keychain] CryptProtectData failed err=%lu service=%s\n",
                 GetLastError(), service);
        return false;
    }

    std::filesystem::path path = item_path(service, account);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    // Write to a sibling temp file then rename so a crash mid-write never
    // leaves a truncated blob behind.
    std::filesystem::path tmp = path;
    tmp += L".tmp";
    bool ok = false;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (f) {
            f.write((const char*)enc.pbData, enc.cbData);
            ok = f.good();
        }
    }
    LocalFree(enc.pbData);
    if (!ok) {
        std::filesystem::remove(tmp, ec);
        LOG_INFO("[keychain] write failed service=%s\n", service);
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        LOG_INFO("[keychain] rename failed err=%lu service=%s\n", GetLastError(), service);
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

static bool delete_item(const char* service, const std::string& account) {
    std::error_code ec;
    std::filesystem::remove(item_path(service, account), ec);
    return !ec;
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

#else
#error "keychain_store: no secret-storage backend for this platform"
#endif

} // namespace keychain_store
