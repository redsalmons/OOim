#ifndef PERSISTENCE_KEY_REPO_H
#define PERSISTENCE_KEY_REPO_H

#include <string>
#include <cstdint>
#include <vector>

struct CodeRecord {
    int64_t id = 0;
    std::string account;
    std::string pubkey;
    std::string secretkey;
    std::string identify;
};

struct KeyInfoRecord {
    std::string pub;
    std::string key;
    std::string password;
    std::string account;
    int sessionId = 0;
};

class KeyRepo {
public:
    // --- code table ---

    // Upsert code record by account (check exists -> update or insert)
    bool upsertCode(const std::string& account, const std::string& pubkey,
        const std::string& secretkey, const std::string& sessionUuid);

    // Query pubkey by account and session
    std::string queryPubkeyByAccountAndSession(const std::string& account, const std::string& sessionUuid);

    // Query latest pubkey from code table by account
    std::string queryPubkeyByAccount(const std::string& account);

    // Query code records by account (all, ordered by id DESC)
    std::vector<CodeRecord> queryCodeByAccount(const std::string& account);

    // Query code by identify (MD5 of pubkey)
    CodeRecord queryCodeByIdentify(const std::string& identify);

    // --- keyinfo table ---

    // Insert keyinfo record
    bool insertKeyInfo(const std::string& pub, const std::string& key,
        const std::string& password, const std::string& sessionUuid, const std::string& account);

    // Query latest pubkey from keyinfo by account
    std::string queryLatestPubkeyFromKeyInfo(const std::string& account);

    // Query private key from keyinfo by account + expected md5 of pubkey
    // Returns true if found, fills privPem and keyPassword
    bool queryPrivateKeyByAccountAndMd5(const std::string& account,
        const std::string& expectedMd5,
        std::string& outPrivPem, std::string& outKeyPassword);

    // Query full keypair from keyinfo by session_uuid
    // Returns true if found, fills pubkey, privPem and keyPassword
    bool queryKeyInfoBySession(const std::string& sessionUuid,
        std::string& outPubkey, std::string& outPrivPem, std::string& outKeyPassword);
};

#endif // PERSISTENCE_KEY_REPO_H
