#ifndef SIGNAL_PROTOCOL_H
#define SIGNAL_PROTOCOL_H

#include <string>
#include <vector>
#include <cstdint>

// Signal protocol over Email: X3DH + Double Ratchet
// Uses existing ECC functions from email_core_common.h

// --- Data structures ---

struct PrekeyBundle {
    std::string ikPub;      // Identity key (PEM)
    std::string spkPub;     // Signed Prekey (PEM)
    std::string spkSig;     // Signature of SPK by IK (base64)
    std::string opkPub;     // One-Time Prekey (PEM), can be empty
};

struct SignalHeader {
    std::string dhPub;      // Sender's current DH ratchet public key (PEM)
    int n = 0;              // Message number in sending chain
    int pn = 0;             // Previous chain length (number of messages in previous receiving chain)
    std::string msgType;    // "init" | "msg" | "repair"
};

struct EncryptedMessage {
    SignalHeader header;
    std::string ciphertext; // base64 encoded (AES-256-CBC encrypted payload)
    // For SESSION_INIT messages, also includes:
    std::string ekPub;      // Ephemeral key public part (PEM), only for init
    std::string ikPub;      // Sender's identity key (PEM), only for init
    std::string opkId;      // OPK identifier used (empty if none), only for init
};

struct DecryptResult {
    bool success = false;
    std::string plaintext;
    std::string sessionId;
    std::string error;
};

// --- X3DH ---

// Initiator side: Alice computes Root Key from Bob's Prekey Bundle
// Returns Root Key (32 bytes, base64 encoded)
// Also generates ephemeral key pair (ekPub, ekPriv) for the init message
bool x3dh_initiate(
    const std::string& aliceAccount,
    const PrekeyBundle& bobBundle,
    std::string& outRootKey,       // base64
    std::string& outEkPub,         // PEM
    std::string& outEkPriv,        // encrypted PEM
    std::string& outEkPassword     // password for ekPriv
);

// Responder side: Bob computes Root Key from Alice's init message
// Uses Bob's local identity key, signed prekey, and (optionally) one-time prekey
bool x3dh_respond(
    const std::string& bobAccount,
    const std::string& aliceIkPub,   // Alice's identity key (PEM)
    const std::string& aliceEkPub,   // Alice's ephemeral key (PEM)
    std::string& outRootKey          // base64
);

// --- Double Ratchet ---

// Initialize DR state from X3DH root key
// Called by initiator (Alice) with Bob's signed prekey as initial peer DH
// Called by responder (Bob) with Alice's ephemeral key as initial peer DH
bool dr_initialize(
    const std::string& account,
    const std::string& peerEmail,
    const std::string& sessionId,
    const std::string& rootKey,      // base64
    const std::string& peerDhPub,    // PEM (peer's initial DH public key)
    bool isInitiator                 // true=Alice (has send chain), false=Bob (has recv chain)
);

// Encrypt a message using the current sending chain
// Returns the signal header + ciphertext
bool dr_encrypt(
    const std::string& account,
    const std::string& peerEmail,
    const std::string& sessionId,
    const std::string& plaintext,
    EncryptedMessage& outMsg
);

// Decrypt a message using the receiving chain
// Handles DH ratchet and skipped keys
DecryptResult dr_decrypt(
    const std::string& account,
    const std::string& peerEmail,
    const std::string& sessionId,
    const EncryptedMessage& msg
);

// --- Utility ---

// Generate a new session ID
std::string generate_session_id();

// Ensure identity key exists for an account (generate if not)
bool ensure_identity_key(const std::string& account);

// Ensure signed prekey exists for an account (generate if not)
bool ensure_signed_prekey(const std::string& account);

// Generate one-time prekeys if supply is low
bool ensure_one_time_prekeys(const std::string& account, int minCount = 5);

// Build a PrekeyBundle for an account (for sending in-band)
PrekeyBundle build_prekey_bundle(const std::string& account);

// Per-session variants: use sessionUuid (conversation id) as logical key
bool ensure_identity_key_for_session(const std::string& sessionUuid);
bool ensure_signed_prekey_for_session(const std::string& sessionUuid);
bool ensure_one_time_prekeys_for_session(const std::string& sessionUuid, int minCount = 5);
PrekeyBundle build_prekey_bundle_for_session(const std::string& sessionUuid);

// Per-session X3DH: load identity/prekey via sessionUuid instead of account
bool x3dh_initiate_for_session(
    const std::string& sessionUuid,
    const PrekeyBundle& bobBundle,
    std::string& outRootKey,
    std::string& outEkPub,
    std::string& outEkPriv,
    std::string& outEkPassword
);
bool x3dh_respond_for_session(
    const std::string& sessionUuid,
    const std::string& aliceIkPub,
    const std::string& aliceEkPub,
    std::string& outRootKey
);

// Per-session DR init: responder loads SPK via sessionUuid
bool dr_initialize_for_session(
    const std::string& sessionUuid,
    const std::string& account,
    const std::string& peerEmail,
    const std::string& sessionId,
    const std::string& rootKey,
    const std::string& peerDhPub,
    bool isInitiator
);

// DH operation: compute shared secret from our private key + peer public key
std::string dh_compute_shared_secret(const std::string& ourPrivPem, const std::string& ourPassword,
                                     const std::string& peerPubPem);

// KDF: derive root key and chain key from DH output
// Uses HKDF-like construction (SHA256) with old root key as salt (standard KDF_RK)
void kdf_rk_ck(const std::string& dhOutput, const std::string& oldRootKey,
               std::string& outRootKey, std::string& outChainKey);

// KDF for message key: derive message key from chain key
// Also advances the chain key
void kdf_mk(const std::string& chainKey, std::string& outMessageKey, std::string& outNewChainKey);

#endif // SIGNAL_PROTOCOL_H
