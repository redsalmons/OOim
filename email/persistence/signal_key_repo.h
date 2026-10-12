#ifndef PERSISTENCE_SIGNAL_KEY_REPO_H
#define PERSISTENCE_SIGNAL_KEY_REPO_H

#include <string>
#include <cstdint>

// Long-lived identity key of a local account
struct SignalIdentityRecord {
    int64_t id = 0;
    std::string account;
    std::string ikPub;       // PEM
    std::string ikPriv;      // encrypted PEM
    std::string ikPassword;  // password for decrypting ikPriv
};

// Per-session signed prekey of a local account
struct SignalPrekeyRecord {
    int64_t id = 0;
    std::string account;
    std::string sessionUuid; // sig_xxx this prekey belongs to
    std::string pub;         // PEM
    std::string priv;        // encrypted PEM
    std::string password;    // password for decrypting priv
    std::string signature;   // base64 ECDSA(IK, pub)
};

// Peer prekey bundle received in-band, scoped to one session
struct SignalPeerPrekeyRecord {
    int64_t id = 0;
    std::string account;     // local account that received it
    std::string peerEmail;   // who the prekey belongs to
    std::string sessionUuid; // sig_xxx
    std::string ikPub;
    std::string spkPub;
    std::string spkSig;
};

// Trust-on-first-use record of a peer's identity key
struct SignalPeerIdentityRecord {
    int64_t id = 0;
    std::string account;
    std::string peerEmail;
    std::string ikPub;
};

class SignalKeyRepo {
public:
    // --- Account identity key ---
    bool upsertIdentity(const std::string& account, const std::string& ikPub,
                        const std::string& ikPriv, const std::string& ikPassword);
    bool loadIdentity(const std::string& account, SignalIdentityRecord& out);

    // --- Per-session signed prekey ---
    bool insertSignedPrekey(const std::string& account, const std::string& sessionUuid,
                            const std::string& pub, const std::string& priv,
                            const std::string& password, const std::string& signature);
    bool loadSignedPrekey(const std::string& account, const std::string& sessionUuid,
                          SignalPrekeyRecord& out);
    // The responder locates the SPK the initiator used by its public key.
    bool loadSignedPrekeyByPub(const std::string& account, const std::string& pubPem,
                               SignalPrekeyRecord& out);

    // --- Peer prekey bundle (per session) ---
    bool upsertPeerPrekey(const std::string& account, const std::string& peerEmail,
                          const std::string& sessionUuid, const std::string& ikPub,
                          const std::string& spkPub, const std::string& spkSig);
    bool loadPeerPrekey(const std::string& account, const std::string& peerEmail,
                        const std::string& sessionUuid, SignalPeerPrekeyRecord& out);

    // --- Peer identity (TOFU) ---
    bool loadPeerIdentity(const std::string& account, const std::string& peerEmail,
                          SignalPeerIdentityRecord& out);
    bool upsertPeerIdentity(const std::string& account, const std::string& peerEmail,
                            const std::string& ikPub);
    bool deletePeerIdentity(const std::string& account, const std::string& peerEmail);
};

#endif // PERSISTENCE_SIGNAL_KEY_REPO_H
