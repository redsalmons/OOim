#ifndef PERSISTENCE_SIGNAL_SESSION_REPO_H
#define PERSISTENCE_SIGNAL_SESSION_REPO_H

#include <string>
#include <cstdint>
#include <vector>

// Double Ratchet session state
struct SignalSessionRecord {
    int64_t id = 0;
    std::string account;        // local account
    std::string peerEmail;      // remote peer
    std::string sessionId;      // unique session identifier
    std::string rootKey;        // base64
    std::string sendChainKey;   // base64
    std::string recvChainKey;   // base64
    int sendN = 0;
    int recvN = 0;
    int prevSendN = 0;         // length of the previous sending chain (header pn)
    std::string dhSelfPriv;     // encrypted PEM
    std::string dhSelfPub;      // PEM
    std::string dhSelfPassword; // password for dhSelfPriv
    std::string dhPeerPub;      // PEM
    int status = 0;             // 0=active, 1=closed
};

// Skipped message key (for out-of-order messages)
struct SignalSkippedKeyRecord {
    int64_t id = 0;
    std::string sessionId;
    std::string peerDhPub;
    int msgIndex = 0;
    std::string messageKey;     // base64
};

class SignalSessionRepo {
public:
    // Create or replace a session
    bool saveSession(const SignalSessionRecord& rec);

    // Load session by (account, peerEmail, sessionId)
    bool loadSession(const std::string& account, const std::string& peerEmail,
                     const std::string& sessionId, SignalSessionRecord& out);

    // Load active session by (account, peerEmail) — returns the most recent active one
    bool loadActiveSession(const std::string& account, const std::string& peerEmail,
                           SignalSessionRecord& out);

    // Get peer email by (account, sessionId)
    std::string getPeerEmail(const std::string& account, const std::string& sessionId);

    // Check if a session exists
    bool sessionExists(const std::string& account, const std::string& peerEmail,
                       const std::string& sessionId);

    // Record that the key exchange with this session's peer is complete: this side holds
    // the peer's keys and the peer provably holds ours. Set once, never cleared by the
    // double-ratchet save path.
    bool markKexDone(const std::string& account, const std::string& sessionId);

    // Whether the key exchange for (account, sessionId) has completed.
    bool isKexDone(const std::string& account, const std::string& sessionId);

    // Close a session (set status=1)
    bool closeSession(const std::string& account, const std::string& peerEmail,
                      const std::string& sessionId);

    // --- Skipped keys ---

    // Insert a skipped key
    bool insertSkippedKey(const std::string& sessionId, const std::string& peerDhPub,
                          int msgIndex, const std::string& messageKey);

    // Find a skipped key by (sessionId, peerDhPub, msgIndex)
    bool findSkippedKey(const std::string& sessionId, const std::string& peerDhPub,
                        int msgIndex, SignalSkippedKeyRecord& out);

    // Delete a skipped key after use
    bool deleteSkippedKey(const std::string& sessionId, const std::string& peerDhPub,
                          int msgIndex);

    // Count skipped keys for a session (for limiting)
    int countSkippedKeys(const std::string& sessionId);
};

#endif // PERSISTENCE_SIGNAL_SESSION_REPO_H
