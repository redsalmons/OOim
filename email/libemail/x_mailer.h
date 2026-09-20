#ifndef LIBEMAIL_X_MAILER_H
#define LIBEMAIL_X_MAILER_H

#include <string>

// X-Mailer header values for message type classification
namespace XMailer {

// Header name
constexpr const char* HEADER = "X-Mailer";

// Message type values (Signal protocol over Email, v1)
constexpr const char* PREKEY_BUNDLE  = "1.0.0";  // Prekey distribution (in-band)
constexpr const char* SESSION_INIT   = "1.0.1";  // X3DH session init + first encrypted message
constexpr const char* RATCHET_MSG    = "1.0.2";  // Double Ratchet encrypted message
constexpr const char* REPAIR_MSG     = "1.0.3";  // Session repair / reset
constexpr const char* ATTACH_META    = "1.0.4";  // Encrypted attachment metadata
constexpr const char* ATTACH_CHUNK   = "1.0.5";  // Attachment chunk data

// Group messaging (MLS / RFC 9420, v2.0). Independent from the 1:1 Double Ratchet channel.
// Body is JSON; the chain uses body x_message_id / x_reply_to.
constexpr const char* MLS_KEY_PACKAGE = "2.0.0";  // body.type = mls_invite (root, from owner) | mls_key_package (member -> owner)
constexpr const char* MLS_WELCOME     = "2.0.1";  // owner -> member: Welcome + RatchetTree
constexpr const char* MLS_COMMIT      = "2.0.2";  // owner -> existing members: Commit (tree change)
constexpr const char* MLS_APP_MSG     = "2.0.3";  // group application message (MLS ciphertext)
constexpr const char* MLS_FILE_META   = "2.0.4";  // group file metadata (visible in UI)
constexpr const char* MLS_FILE_CHUNK  = "2.0.5";  // group file chunk data (hidden from UI)

inline bool isMls(const std::string& v) {
    return v == MLS_KEY_PACKAGE || v == MLS_WELCOME || v == MLS_COMMIT || v == MLS_APP_MSG ||
           v == MLS_FILE_META || v == MLS_FILE_CHUNK;
}

// Aliases mapping old names to new 1.0.x values (no 0.1.x backward compat)
constexpr const char* NEW_SESSION  = SESSION_INIT;   // New session creation
constexpr const char* EXCHANGE     = PREKEY_BUNDLE;  // Key exchange (prekey bundle)
constexpr const char* TEXT         = RATCHET_MSG;    // Encrypted text message
constexpr const char* FILE_META    = ATTACH_META;    // File metadata (visible in UI)
constexpr const char* FILE_CHUNK   = ATTACH_CHUNK;   // File chunk (hidden from UI)

// Legacy 0.1.x values from older builds
constexpr const char* LEGACY_PREKEY_BUNDLE = "0.1.0";
constexpr const char* LEGACY_SESSION_INIT  = "0.1.1";
constexpr const char* LEGACY_RAT_MSG       = "0.1.2";

// Whitelist of all valid X-Mailer values
inline bool isValid(const std::string& value) {
    return value == PREKEY_BUNDLE ||
           value == SESSION_INIT ||
           value == RATCHET_MSG ||
           value == REPAIR_MSG ||
           value == ATTACH_META ||
           value == ATTACH_CHUNK ||
           isMls(value) ||
           value == LEGACY_PREKEY_BUNDLE ||
           value == LEGACY_SESSION_INIT ||
           value == LEGACY_RAT_MSG;
}

} // namespace XMailer

#endif // LIBEMAIL_X_MAILER_H
