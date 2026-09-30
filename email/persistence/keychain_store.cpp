#include "keychain_store.h"
#include "logger.h"
#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>
#include <string.h>

namespace keychain_store {

static const char* kServiceDb = "com.redsalmon.oim.db";
static const char* kServiceCreds = "com.redsalmon.oim.credentials";
static const char* kAccountMaster = "master";

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

static std::string hex_encode(const uint8_t* p, size_t n) {
    static const char* t = "0123456789abcdef";
    std::string out(n * 2, '0');
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = t[p[i] >> 4];
        out[2 * i + 1] = t[p[i] & 0xf];
    }
    return out;
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

} // namespace keychain_store
