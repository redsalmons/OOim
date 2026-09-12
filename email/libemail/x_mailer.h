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

// Aliases mapping old names to new 1.0.x values (no 0.1.x backward compat)
constexpr const char* NEW_SESSION  = SESSION_INIT;   // New session creation
constexpr const char* EXCHANGE     = PREKEY_BUNDLE;  // Key exchange (prekey bundle)
constexpr const char* TEXT         = RATCHET_MSG;    // Encrypted text message
constexpr const char* FILE_META    = ATTACH_META;    // File metadata (visible in UI)
constexpr const char* FILE_CHUNK   = ATTACH_CHUNK;   // File chunk (hidden from UI)

// Whitelist of all valid X-Mailer values
inline bool isValid(const std::string& value) {
    return value == PREKEY_BUNDLE ||
           value == SESSION_INIT ||
           value == RATCHET_MSG ||
           value == REPAIR_MSG ||
           value == ATTACH_META ||
           value == ATTACH_CHUNK;
}

} // namespace XMailer

#endif // LIBEMAIL_X_MAILER_H
