#ifndef SIGNAL_SENDER_KEY_H
#define SIGNAL_SENDER_KEY_H

#include <string>
#include <cstdint>

// Sender Key symmetric ratchet for group messaging
// Implements the Sender Keys component of the Signal/WhatsApp group protocol

// --- Data structures ---

// Result of a group encrypt operation
struct GroupEncryptResult {
    bool success = false;
    std::string groupId;
    std::string senderEmail;
    int iteration = 0;
    int epoch = 0;
    std::string ciphertext;   // base64 (IV + encrypted)
    std::string signature;    // base64
    std::string error;
};

// Result of a group decrypt operation
struct GroupDecryptResult {
    bool success = false;
    std::string plaintext;
    std::string error;
};

// --- Sender Key generation ---

// Generate a new Sender Key (chain key + signing key pair) for a group
// Returns the distribution payload as JSON:
// {"type":"sender_key", "group_id":"...", "chain_key":"base64", "signing_pub":"base64", "epoch":N}
std::string sender_key_generate(const std::string& account, const std::string& groupId);

// Get the distribution payload for an existing Sender Key
// (so it can be sent to other group members via 1:1 DR)
std::string sender_key_get_distribution(const std::string& account, const std::string& groupId);

// Store a received Sender Key from another member
bool sender_key_store(const std::string& account, const std::string& groupId,
                      const std::string& senderEmail,
                      const std::string& chainKey, const std::string& signingPub,
                      int epoch);

// Check if we have Sender Keys for all members of a group
// Returns 1 if ready, 0 if not
int sender_key_check_ready(const std::string& account, const std::string& groupId);

// --- Group message encrypt/decrypt ---

// Encrypt a group message using our own Sender Key
// Advances the symmetric ratchet (chain key step)
GroupEncryptResult group_encrypt(const std::string& account,
                                  const std::string& groupId,
                                  const std::string& plaintext);

// Decrypt a group message from a specific sender
// Handles chain key stepping and skipped keys for out-of-order messages
GroupDecryptResult group_decrypt(const std::string& account,
                                  const std::string& groupId,
                                  const std::string& senderEmail,
                                  int iteration, int epoch,
                                  const std::string& ciphertext,
                                  const std::string& signature);

#endif // SIGNAL_SENDER_KEY_H
