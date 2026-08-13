#ifndef SYNC_SESSION_H
#define SYNC_SESSION_H

#include <string>
#include <map>
#include <memory>
#include <set>

namespace oemail {

// class ImapOauth;  // IMAP layer removed

// Session structure to store sync state across the entire sync cycle
struct session_t {
    // Auth cache to avoid multiple token refreshes per sync cycle
    // IMAP layer removed - auth cache disabled
    // std::map<std::string, std::shared_ptr<ImapOauth>> auth_cache;

    // Folder creation status to avoid multiple creation attempts per sync cycle
    // Key: email address, Value: set of folder names that have been checked/created
    std::map<std::string, std::set<std::string>> folder_creation_status;

    // Check if a folder has been processed for this email
    bool is_folder_processed(const std::string& email, const std::string& folder_name) {
        auto it = folder_creation_status.find(email);
        if (it != folder_creation_status.end()) {
            return it->second.find(folder_name) != it->second.end();
        }
        return false;
    }

    // Mark a folder as processed for this email
    void mark_folder_processed(const std::string& email, const std::string& folder_name) {
        folder_creation_status[email].insert(folder_name);
    }

    // Get cached auth object for email
    // IMAP layer removed - disabled
    // std::shared_ptr<ImapOauth> get_cached_auth(const std::string& email) {
    //     auto it = auth_cache.find(email);
    //     if (it != auth_cache.end()) {
    //         return it->second;
    //     }
    //     return nullptr;
    // }

    // Cache auth object for email
    // IMAP layer removed - disabled
    // void cache_auth(const std::string& email, std::shared_ptr<ImapOauth> auth) {
    //     auth_cache[email] = auth;
    // }

    // Clear all session state (call at start of sync cycle)
    void clear() {
        // auth_cache.clear();  // IMAP layer removed
        folder_creation_status.clear();
    }
};

} // namespace oemail

#endif // SYNC_SESSION_H
