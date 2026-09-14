#ifndef PERSISTENCE_SENDER_KEY_REPO_H
#define PERSISTENCE_SENDER_KEY_REPO_H

#include <string>
#include <cstdint>
#include <vector>

// Sender Key state for group messaging (symmetric ratchet)
struct SenderKeyRecord {
    int64_t id = 0;
    std::string groupId;
    std::string account;          // local account
    std::string senderEmail;      // who owns this SenderKey
    std::string chainKey;         // base64
    std::string signingKeyPub;    // base64
    std::string signingKeyPriv;   // base64, only for own keys
    int iteration = 0;
    int epoch = 0;
    int status = 0;               // 0=active, 1=closed
};

// Skipped sender message key (for out-of-order group messages)
struct SkippedSenderKeyRecord {
    int64_t id = 0;
    std::string groupId;
    std::string account;
    std::string senderEmail;
    int iteration = 0;
    int epoch = 0;
    std::string cipherKey;       // base64
    std::string iv;               // base64
    std::string signingKey;       // base64
};

class SenderKeyRepo {
public:
    // Save or update a SenderKey
    bool saveSenderKey(const SenderKeyRecord& rec);

    // Load SenderKey by (groupId, account, senderEmail, epoch)
    bool loadSenderKey(const std::string& groupId, const std::string& account,
                       const std::string& senderEmail, int epoch,
                       SenderKeyRecord& out);

    // Load active SenderKey by (groupId, account, senderEmail) — latest epoch
    bool loadActiveSenderKey(const std::string& groupId, const std::string& account,
                             const std::string& senderEmail,
                             SenderKeyRecord& out);

    // Update chain key and iteration for a SenderKey
    bool updateChainKey(const std::string& groupId, const std::string& account,
                        const std::string& senderEmail, int epoch,
                        const std::string& chainKey, int iteration);

    // Close all SenderKeys for a group (set status=1)
    bool closeSenderKeys(const std::string& groupId, const std::string& account);

    // --- Skipped keys ---

    bool insertSkippedKey(const SkippedSenderKeyRecord& rec);

    bool findSkippedKey(const std::string& groupId, const std::string& account,
                        const std::string& senderEmail, int iteration, int epoch,
                        SkippedSenderKeyRecord& out);

    bool deleteSkippedKey(const std::string& groupId, const std::string& account,
                          const std::string& senderEmail, int iteration, int epoch);

    int countSkippedKeys(const std::string& groupId, const std::string& account,
                         const std::string& senderEmail);
};

#endif // PERSISTENCE_SENDER_KEY_REPO_H
