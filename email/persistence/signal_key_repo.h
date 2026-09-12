#ifndef PERSISTENCE_SIGNAL_KEY_REPO_H
#define PERSISTENCE_SIGNAL_KEY_REPO_H

#include <string>
#include <cstdint>
#include <vector>

// Identity key record for an account
struct SignalIdentityRecord {
    int64_t id = 0;
    std::string account;
    std::string sessionUuid;  // optional per-session key scope
    std::string ikPub;       // PEM
    std::string ikPriv;      // encrypted PEM
    std::string ikPassword;  // password for decrypting ikPriv
};

// Prekey record (signed prekey or one-time prekey)
struct SignalPrekeyRecord {
    int64_t id = 0;
    std::string account;
    std::string sessionUuid; // optional per-session key scope
    int keyType = 0;         // 0=signed_prekey, 1=one_time_prekey
    std::string pub;         // PEM
    std::string priv;        // encrypted PEM
    std::string password;    // password for decrypting priv
    std::string signature;   // base64 signature (only for signed_prekey)
    int used = 0;            // 0=available, 1=used
};

// Peer prekey bundle (received in-band from another user)
struct SignalPeerPrekeyRecord {
    int64_t id = 0;
    std::string account;     // local account that received it
    std::string peerEmail;   // who the prekey belongs to
    std::string sessionUuid; // optional per-session key scope
    std::string ikPub;
    std::string spkPub;
    std::string spkSig;
    std::string opkPub;
    int version = 1;
};

class SignalKeyRepo {
public:
    // --- Identity keys ---

    // Insert or update identity key for an account
    bool upsertIdentity(const std::string& account, const std::string& ikPub,
                        const std::string& ikPriv, const std::string& ikPassword);

    // Load identity key for an account
    bool loadIdentity(const std::string& account, SignalIdentityRecord& out);

    // Insert or update identity key scoped to a session (sessionUuid is global in-app)
    bool upsertIdentityForSession(const std::string& sessionUuid,
                                  const std::string& ikPub,
                                  const std::string& ikPriv,
                                  const std::string& ikPassword);

    // Load identity key for a session
    bool loadIdentityForSession(const std::string& sessionUuid, SignalIdentityRecord& out);

    // Load identity key by its public key (searches all sessions/accounts)
    bool loadIdentityByPub(const std::string& pubPem, SignalIdentityRecord& out);

    // --- Signed Prekey ---

    // Insert signed prekey
    bool insertSignedPrekey(const std::string& account, const std::string& pub,
                            const std::string& priv, const std::string& password,
                            const std::string& signature);

    // Load current signed prekey for an account
    bool loadSignedPrekey(const std::string& account, SignalPrekeyRecord& out);

    // Insert signed prekey scoped to a session
    bool insertSignedPrekeyForSession(const std::string& sessionUuid,
                                      const std::string& pub,
                                      const std::string& priv,
                                      const std::string& password,
                                      const std::string& signature);

    // Load current signed prekey for a session
    bool loadSignedPrekeyForSession(const std::string& sessionUuid, SignalPrekeyRecord& out);

    // Load signed prekey by its public key (searches all sessions/accounts)
    bool loadSignedPrekeyByPub(const std::string& pubPem, SignalPrekeyRecord& out);

    // --- One-Time Prekeys ---

    // Insert one-time prekey
    bool insertOneTimePrekey(const std::string& account, const std::string& pub,
                             const std::string& priv, const std::string& password);

    // Claim an available one-time prekey (mark as used, return it)
    bool claimOneTimePrekey(const std::string& account, SignalPrekeyRecord& out);

    // Count available one-time prekeys
    int countAvailableOneTimePrekeys(const std::string& account);

    // Insert one-time prekey scoped to a session
    bool insertOneTimePrekeyForSession(const std::string& sessionUuid,
                                       const std::string& pub,
                                       const std::string& priv,
                                       const std::string& password);

    // Claim an available one-time prekey for a session
    bool claimOneTimePrekeyForSession(const std::string& sessionUuid, SignalPrekeyRecord& out);

    // --- Peer Prekey Cache ---

    // Upsert peer prekey bundle (optionally scoped to a session via keyScope)
    bool upsertPeerPrekey(const std::string& account, const std::string& peerEmail,
                          const std::string& ikPub, const std::string& spkPub,
                          const std::string& spkSig, const std::string& opkPub,
                          const std::string& keyScope = "");

    // Load peer prekey bundle
    bool loadPeerPrekey(const std::string& account, const std::string& peerEmail,
                        SignalPeerPrekeyRecord& out);

    // Upsert peer prekey bundle scoped to a session
    bool upsertPeerPrekeyForSession(const std::string& sessionUuid,
                                    const std::string& peerEmail,
                                    const std::string& ikPub,
                                    const std::string& spkPub,
                                    const std::string& spkSig,
                                    const std::string& opkPub);

    // Load peer prekey bundle for a session
    bool loadPeerPrekeyForSession(const std::string& sessionUuid,
                                  const std::string& peerEmail,
                                  SignalPeerPrekeyRecord& out);
};

#endif // PERSISTENCE_SIGNAL_KEY_REPO_H
