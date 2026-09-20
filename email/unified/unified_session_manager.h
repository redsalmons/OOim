#ifndef UNIFIED_SESSION_MANAGER_H
#define UNIFIED_SESSION_MANAGER_H

#include "unified_session.h"
#include "unified_session_repo.h"
#include <string>
#include <vector>

// Unified Session Manager — the single entry point for all encrypted messaging.
//
// Routing rules:
//   - 2 members  → Signal 1:1 (Double Ratchet / X3DH)
//   - 3+ members → MLS 1:n (Ratchet Tree / RFC 9420)
//   - Adding a member to a Signal session with >2 total → irreversible upgrade to MLS
//
// The GUI layer should only interact with this class; protocol details are hidden.
class UnifiedSessionManager {
public:
    // Create a new encrypted session.
    // members must include the local account + all remote participants.
    // Auto-selects Signal (2 members) or MLS (3+ members).
    SendResult createSession(const std::string& account,
                             const std::string& subject,
                             const std::vector<std::string>& members,
                             const std::vector<std::string>& mailmen = {},
                             int pinned = 0,
                             int hidden = 0);

    // Send a message in an existing session.
    // Routes to Signal or MLS encryption based on the session's mode.
    // inReplyTo: x_message_id of the previous message (for threading).
    SendResult sendMessage(const std::string& account,
                           const std::string& sessionId,
                           const std::string& plaintext,
                           const std::string& inReplyTo);

    // Handle an incoming encrypted message.
    // Auto-detects protocol from xMailer and routes to the correct decryptor.
    RecvResult handleIncoming(const std::string& account,
                              const std::string& from,
                              const std::string& xMailer,
                              const std::string& body,
                              const std::string& messageId,
                              const std::string& inReplyTo);

    // Add members to an existing session.
    // If the session is Signal and the total member count exceeds 2, the session
    // is automatically (and irreversibly) upgraded to MLS.
    // inReplyTo: x_reply_to for the upgrade invite (last message id of the
    // conversation; pass "" when unknown).
    SendResult addMembers(const std::string& account,
                          const std::string& sessionId,
                          const std::vector<std::string>& newMembers,
                          const std::string& inReplyTo = "");

    // Query all active sessions for an account.
    std::vector<UnifiedSession> listSessions(const std::string& account);

    // Get a specific session by its unified session_id.
    UnifiedSession getSession(const std::string& sessionId);

    // Check if a session is ready for sending (keys exchanged, MLS joined, etc.).
    bool isReady(const std::string& account, const std::string& sessionId);

    // Update the pinned/hidden display flags of a session.
    bool setSessionFlags(const std::string& sessionId, int pinned, int hidden);

    // Double-Ratchet-encrypt an arbitrary plaintext for a Signal (1:1) session.
    // Output is the wire envelope JSON, sized dynamically (used for large file
    // chunks). Returns false with `error` set when the session is not Signal or
    // not yet established.
    bool signalEncrypt(const std::string& account, const std::string& sessionId,
                       const std::string& plaintext, const std::string& messageId,
                       const std::string& inReplyTo,
                       std::string& outEnvelope, std::string& error);

private:
    UnifiedSessionRepo repo_;

    // Internal: create a 1:1 Signal session.
    SendResult createSignalSession_(const std::string& account,
                                    const std::string& subject,
                                    const std::vector<std::string>& members,
                                    const std::vector<std::string>& mailmen,
                                    int pinned,
                                    int hidden);

    // Internal: create a 1:n MLS session.
    SendResult createMlsSession_(const std::string& account,
                                 const std::string& subject,
                                 const std::vector<std::string>& members,
                                 const std::vector<std::string>& mailmen,
                                 int pinned,
                                 int hidden);

    // Internal: upgrade a Signal session to MLS (irreversible).
    SendResult upgradeToMls_(const std::string& account,
                             UnifiedSession& existingSession,
                             const std::vector<std::string>& additionalMembers,
                             const std::string& inReplyTo);

    // Internal: generate a unique session id.
    static std::string generateSessionId();

    // Internal: generate a unique message id.
    static std::string generateMessageId(const std::string& account);
};

#endif // UNIFIED_SESSION_MANAGER_H
