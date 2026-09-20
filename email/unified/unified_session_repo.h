#ifndef UNIFIED_SESSION_REPO_H
#define UNIFIED_SESSION_REPO_H

#include "unified_session.h"
#include <sqlite3.h>
#include <string>
#include <vector>

// Persistence layer for the unified_session table.
// All methods are thread-safe (they acquire the global DB mutex internally).
class UnifiedSessionRepo {
public:
    // Insert a new unified session. Returns true on success.
    bool create(const UnifiedSession& rec);

    // Load a session by its unified session_id. Returns true if found.
    bool load(const std::string& sessionId, UnifiedSession& out);

    // Find an active session by matching the exact member set (order-independent).
    // Returns true if found.
    bool loadByMembers(const std::string& account,
                       const std::vector<std::string>& members,
                       UnifiedSession& out);

    // Find an active session by its root message id (the shared conversation key
    // carried as x_session_id in MLS upgrade invites).
    bool loadByRootMessageId(const std::string& account,
                             const std::string& rootMessageId,
                             UnifiedSession& out);

    // Upgrade a session from Signal to MLS (irreversible).
    // Sets mode=mls, mls_group_id, and updates members.
    bool upgradeToMls(const std::string& sessionId,
                      const std::string& mlsGroupId,
                      const std::vector<std::string>& newMembers);

    // Update the member list of a session (for MLS addMembers).
    bool updateMembers(const std::string& sessionId,
                       const std::vector<std::string>& members);

    // Update the signal_session_id of a session (after SMTP send establishes it).
    bool updateSignalSessionId(const std::string& sessionId,
                               const std::string& signalSessionId);

    // List all active sessions for an account.
    std::vector<UnifiedSession> listByAccount(const std::string& account);

    // Close a session (set status=1).
    bool close(const std::string& sessionId);

private:
    // Helper: serialize members vector to JSON array string.
    static std::string membersToJson(const std::vector<std::string>& members);
    // Helper: deserialize JSON array string to members vector.
    static std::vector<std::string> jsonToMembers(const std::string& json);
    // Helper: fill a UnifiedSession from a sqlite3_stmt (columns must match SELECT order).
    static void fillRecord(sqlite3_stmt* stmt, UnifiedSession& out);
};

#endif // UNIFIED_SESSION_REPO_H
