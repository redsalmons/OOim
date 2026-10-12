#ifndef SIGNAL_PROTOCOL_H
#define SIGNAL_PROTOCOL_H

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

// Signal protocol over Email: X3DH + Double Ratchet (wire format v2)
//
// Keys:
//   IK  — long-lived identity key, one per local account (P-256). Signs SPKs and
//         is the TOFU anchor for peers (see SignalKeyRepo peer identity).
//   SPK — signed prekey, one per 1:1 session (fresh for every PREKEY_BUNDLE).
//         Because every bundle carries a fresh SPK there are no one-time prekeys.
// Cipher: AES-256-GCM. Message key -> HKDF -> 32B key || 12B nonce. The whole
// envelope except the ciphertext is bound as associated data.

// --- Data structures ---

struct PrekeyBundle {
    std::string ikPub;      // Identity key (PEM)
    std::string spkPub;     // Signed Prekey (PEM)
    std::string spkSig;     // ECDSA signature of spkPub by IK (base64)
};

struct SignalHeader {
    std::string dhPub;      // Sender's current DH ratchet public key (PEM)
    int n = 0;              // Message number in sending chain
    int pn = 0;             // Length of the sender's previous sending chain
    std::string msgType;    // "init" | "msg" | "repair"
};

struct EncryptedMessage {
    SignalHeader header;
    std::string ciphertext; // base64(nonce || ciphertext || tag)
    std::string ekPub;      // Ephemeral key public part (PEM), only for init
    std::string ikPub;      // Sender's identity key (PEM), only for init
};

struct DecryptResult {
    bool success = false;
    std::string plaintext;
    std::string sessionId;
    std::string error;
};

// Builds the associated data for a message once its ratchet header is known.
using AdBuilder = std::function<std::string(const SignalHeader&)>;

// --- Identity & prekeys ---

// Account-level long-lived identity key (generated once).
bool ensure_identity_key(const std::string& account);
std::string identity_public_key(const std::string& account); // PEM, "" if missing
// SHA-256 fingerprint (hex) of a public key PEM; used for safety numbers.
std::string key_fingerprint(const std::string& pubPem);
// ECDSA signature (base64) of data by the account IK; "" if no identity key.
std::string sign_with_identity_key(const std::string& account, const std::string& data);

// Trust-on-first-use for peer identity keys, shared by the 1:1 and group paths:
// the first key seen for (account, peer) is pinned; returns 0 if ikPub matches
// (or was just pinned), -3 if a different key is already pinned.
int pin_peer_identity(const std::string& account, const std::string& peer, const std::string& ikPub);

// Per-session signed prekey, signed by the account IK.
bool ensure_signed_prekey_for_session(const std::string& account, const std::string& sessionUuid);
PrekeyBundle build_prekey_bundle_for_session(const std::string& account, const std::string& sessionUuid);

// spkSig must be a valid IK signature over spkPub.
bool verify_prekey_bundle(const PrekeyBundle& bundle);

// --- X3DH ---

// Initiator: root key from the peer's bundle using the account IK and a fresh EK.
bool x3dh_initiate(const std::string& account,
                   const PrekeyBundle& peerBundle,
                   std::string& outRootKey,       // base64
                   std::string& outEkPub,         // PEM
                   std::string& outEkPriv,        // encrypted PEM
                   std::string& outEkPassword);

// Responder: root key from the initiator's IK/EK using the account IK and the
// per-session SPK the initiator used.
bool x3dh_respond(const std::string& account,
                  const std::string& sessionUuid,
                  const std::string& peerIkPub,
                  const std::string& peerEkPub,
                  std::string& outRootKey);

// --- Double Ratchet ---

// Initiator: peerDhPub is the peer's SPK; derives the first sending chain.
// Responder: own SPK (per sessionUuid) becomes the initial ratchet key.
bool dr_initialize(const std::string& account,
                   const std::string& peerEmail,
                   const std::string& sessionId,
                   const std::string& rootKey,
                   const std::string& peerDhPub,
                   bool isInitiator);

bool dr_encrypt(const std::string& account,
                const std::string& peerEmail,
                const std::string& sessionId,
                const std::string& plaintext,
                const AdBuilder& adBuilder,
                EncryptedMessage& outMsg);

DecryptResult dr_decrypt(const std::string& account,
                         const std::string& peerEmail,
                         const std::string& sessionId,
                         const EncryptedMessage& msg,
                         const std::string& ad);

// --- Utility ---

std::string generate_session_id();

std::string dh_compute_shared_secret(const std::string& ourPrivPem, const std::string& ourPassword,
                                     const std::string& peerPubPem);
void kdf_rk_ck(const std::string& dhOutput, const std::string& oldRootKey,
               std::string& outRootKey, std::string& outChainKey);
void kdf_mk(const std::string& chainKey, std::string& outMessageKey, std::string& outNewChainKey);

#endif // SIGNAL_PROTOCOL_H
