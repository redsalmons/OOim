#ifndef EMAIL_CORE_H
#define EMAIL_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Unified logging - write message to the shared log file
void email_log_write(const char* message);

// Initialize logger - call once at app startup
int email_logger_init(const char* logDir);

// Email structure
typedef struct {
    char* sender;
    char* recipient;
    char* subject;
    char* body;
    char* timestamp;
} Email;

// Email handler structure
typedef struct {
    Email* emails;
    int count;
    int capacity;
} EmailHandler;

// Core functions
EmailHandler* email_handler_create(int initial_capacity);
void email_handler_destroy(EmailHandler* handler);

// Initialize libemail system with app support directory
int email_core_initialize(const char* appSupportDir);

int email_add(EmailHandler* handler, const char* sender, const char* recipient,
              const char* subject, const char* body);
int email_remove(EmailHandler* handler, int index);
Email* email_get(EmailHandler* handler, int index);
int email_count(EmailHandler* handler);

// Email operations
int email_send(const char* recipient, const char* subject, const char* body);
int email_send_with_headers(const char* recipient, const char* subject, const char* body, const char* in_reply_to);
int email_send_via_smtp(const char* smtp_server, int smtp_port,
                        const char* sender_email, const char* auth_code,
                        const char* recipient, const char* subject,
                        const char* body, const char* in_reply_to);
int email_receive(EmailHandler* handler);
int email_search(EmailHandler* handler, const char* query, Email** results, int* result_count);

// Utility functions
void email_free(Email* email);
const char* get_version(void);
int initialize_email_system(void);
void shutdown_email_system(void);

// Account configuration structure
typedef struct {
    char* type;          // "163.com" / "gmail.com" / "outlook.com"
    char* email;
    char* auth_code;
    char* smtp_server;
    int smtp_port;
    char* imap_server;
    int imap_port;
    char* account_type;  // "personal" / "enterprise"
    int authorized;       // 0 = false, 1 = true
    char* id;             // Unique identifier
    int uid;              // Default 0
    char* phrase;         // Encrypted phrase (base64)
    long long folder_size; // Machine folder size in bytes
    int is_default;        // 1 = default message-sending account
} EmailAccountConfig;

// Save account configurations (and local data path) to a config file.
// Returns 0 on success, negative on error.
int email_config_save(const char* path, const char* local_data_path,
                       const EmailAccountConfig* accounts, int count);

// Load account configurations from a config file.
// On success, *accounts is malloc'd (caller must free with email_config_free),
// *count is set, and *local_data_path is malloc'd (caller must free with free()).
// Returns 0 on success, negative on error (e.g. file not found).
int email_config_load(const char* path, char** local_data_path,
                       EmailAccountConfig** accounts, int* count);

// Check whether a config file exists at path. Returns 1 if exists, 0 otherwise.
int email_config_exists(const char* path);

// Free an array of EmailAccountConfig allocated by email_config_load.
void email_config_free(EmailAccountConfig* accounts, int count);

// Save emails from handler to a file. Returns 0 on success, negative on error.
int email_save(EmailHandler* handler, const char* path);

// Load emails from a file into handler. Returns 0 on success, negative on error.
int email_load(EmailHandler* handler, const char* path);

// Chat contact structure
typedef struct {
    char* name;
    char* last_message;
    char* time;
    int unread;
} ChatContact;

// Chat message structure
typedef struct {
    char* sender;
    char* content;
    char* time;
    int is_me;  // 0 = false, 1 = true
} ChatMessage;

// Save chat contacts to a file. Returns 0 on success, negative on error.
int chat_contacts_save(const char* path, const ChatContact* contacts, int count);

// Load chat contacts from a file. Returns 0 on success, negative on error.
int chat_contacts_load(const char* path, ChatContact** contacts, int* count);

// Free chat contacts array allocated by chat_contacts_load.
void chat_contacts_free(ChatContact* contacts, int count);

// Save chat messages to a file. Returns 0 on success, negative on error.
int chat_messages_save(const char* path, const ChatMessage* messages, int count);

// Load chat messages from a file. Returns 0 on success, negative on error.
int chat_messages_load(const char* path, ChatMessage** messages, int* count);

// Free chat messages array allocated by chat_messages_load.
void chat_messages_free(ChatMessage* messages, int count);

// SQLite database handle (opaque pointer)
typedef struct sqlite3 sqlite3;

// Android only: inject the SQLCipher master key (64-char hex) before
// email_db_init. The key is generated and Keystore-wrapped on the Kotlin
// side. Returns 0 on success, negative on unsupported platforms or bad input.
int email_set_db_master_key(const char* hex);

// Initialize SQLite database for email storage. Returns 0 on success, negative on error.
int email_db_init(const char* path);

// Close SQLite database.
void email_db_close(void);

// Insert email into database. Returns 0 on success, negative on error.
int email_db_insert(const char* email, const char* sender, const char* recipient,
                    const char* subject, const char* body, const char* timestamp);

// Query emails from database by email address. Returns 0 on success, negative on error.
// On success, caller must free the returned handler.
int email_db_query(const char* email, EmailHandler* handler);

// IMAP connection handle (opaque pointer)
typedef struct IMAPConnection IMAPConnection;

// Create IMAP connection. Returns NULL on error.
IMAPConnection* imap_create(const char* server, int port, const char* email, const char* auth_code);

// Destroy IMAP connection.
void imap_destroy(IMAPConnection* conn);

// Fetch INBOX emails using PEEK. Returns 0 on success, negative on error.
// Fetched emails are stored in the database.
int imap_fetch_inbox(IMAPConnection* conn);

// Set email credentials on a config index (email address and auth code)
// Returns 0 on success, negative on error.
int email_set_credentials(int configIndex, const char* email, const char* authCode);

// Connect to email server and authenticate. Returns 0 on success, negative on error.
int email_connect(int configIndex);

// List emails in a folder. Returns JSON string in outJson.
// Returns 0 on success, negative on error.
int email_list(int configIndex, const char* folder, char* outJson, int outSize);

// Get email content by UID. Returns JSON string in outJson.
// Returns 0 on success, negative on error.
int email_get_content(int configIndex, const char* folder, const char* uid, char* outJson, int outSize);

// Fetch email headers (uuid, sender, from, subject, date, bodystructure) from IMAP
// and store them in the localemail SQLite table.
// Returns 0 on success, negative on error. outJson contains the fetched emails JSON.
int email_fetch_and_store(int configIndex, const char* folder, const char* startUid,
                          const char* account, const char* storageDir,
                          char* outJson, int outSize);

// Insert sent email into database. Returns 0 on success, negative on error.
int email_insert_sent_email(const char* account, const char* sender, const char* from_addr, const char* to_addr, const char* subject, const char* date, const char* message_id, const char* in_reply_to, const char* body, const char* storageDir, char* outJson, int outSize, const char* x_mailer = nullptr);

// Query session_id by message_id. Returns 0 on success, negative on error.
int email_query_session_by_message_id(const char* messageId, const char* account, char* outSessionId, int outSize);

// Add an email to an existing session by uuid and session_id. Returns 0 on success, negative on error.
int email_add_email_to_session(const char* sessionId, const char* uuid, const char* account, int encrypt_method, char* outJson, int outSize);

// Query localemail table for stored emails by account.
// Returns 0 on success, negative on error. outJson contains the emails JSON.
int email_query_localemail(const char* account, char* outJson, int outSize);

// Query thread root emails (first email of each conversation thread) by account.
// Returns 0 on success, negative on error. outJson contains the thread root emails JSON.
int email_query_thread_roots(const char* account, char* outJson, int outSize);

// Get max UID from localemail table for a specific account.
// Returns 0 on success, negative on error. outUid contains the max UID (or empty if none).
int email_get_max_uid(const char* account, const char* folder, char* outUid, int outSize);

// oemailim C wrappers (for Dart FFI)
int oemailim_system_open(const char* dataDir, const char* configDir, const char* logDir);
int oemailim_open_new_email(const char* email_id);
int oemailim_delete_account_data(const char* account, const char* storageDir);
int oemailim_go(int configIndex);
int oemailim_authority(int configIndex);
void oemailim_system_close(int configIndex);
int oemailim_email_list(int configIndex, const char* path, char* outJson, int outSize);
int oemailim_email_select(int configIndex, const char* path, char* outJson, int outSize);

// Enter IMAP IDLE on the current folder, block until server notification.
// Returns 1 if notification received, 0 if timeout, negative on error.
int email_idle_wait(int configIndex, const char* folder, int timeoutSeconds);

// Discover the Sent folder name via IMAP SPECIAL-USE or name matching.
// Returns 0 on success, negative on error. outFolder contains the folder name.
int email_find_sent_folder(int configIndex, char* outFolder, int outSize);

int email_send_via_config(int configIndex, const char* content);

int email_get_last_error(int configIndex, char* outBuf, int outSize);

// Download email bodies for emails with islocal=0.
// Fetches full body via IMAP, saves to <storageDir>/<account>/<uuid>.eml, updates islocal=1.
// Returns number of emails downloaded, or negative on error.
int email_download_pending_bodies(int configIndex, const char* account,
                                  const char* storageDir, char* outJson, int outSize);

// Count pending email bodies (islocal=0) for a given account.
// Returns count, or negative on error. Does not connect to IMAP.
int email_count_pending_bodies(const char* account);

// Parse the stored body (email_body table, keyed by localemail.id) using vmime
// and return structured JSON: text body, HTML body, attachment info.
// Returns 0 on success, negative on error. outJson contains the parsed result.
int email_parse_body(int64_t emailId, char* outJson, int outSize);

// Update session isread field to 1 for all emails in a session.
// Returns 0 on success, negative on error.
int email_update_session_read(const char* sessionId);

// Query unread count for a session (count of records with isread=0).
// Returns count on success, negative on error.
int email_query_session_unread(const char* sessionId);

// Code table operations (store peer's ECC public key, secretkey, and identify=MD5(pubkey))
int email_code_insert(const char* account, const char* pubkey, const char* secretkey, const char* sessionUuid);
int email_code_query_by_account(const char* account, char* outJson, int outSize);
int email_code_query_by_identify(const char* identify, char* outJson, int outSize);

// Create a new session with ECC key pair generation. Returns 0 on success.
int email_create_session(const char* account, const char* subject, const char* members, const char* message_id, int encrypt_method, int64_t localemail_rowid, char* outJson, int outSize);

// Task table operations for queued email sending
int email_task_insert(const char* account, const char* recipient,
                     const char* subject, const char* body,
                     const char* in_reply_to, const char* message_id,
                     const char* x_message_id, const char* session_id,
                     const char* x_session_chart);
int email_task_query_pending(const char* account, char* outJson, int outSize);
int email_task_mark_sent(int taskId);
int email_task_mark_failed(int taskId);
int email_task_delete(int taskId);
int email_task_process_pending(int configIndex, const char* account, char* outJson, int outSize);

// Query outbox task status by account + message_id (for GUI "sending..." state).
// outJson: {status, task_status: "pending"|"failed"|"none", retry_count, last_error}
int email_task_status(const char* account, const char* messageId, char* outJson, int outSize);

// Migration: Update islocal for existing emails
int email_migrate_islocal();

// --- File Transfer Protocol ---

// Prepare file metadata message JSON (plaintext for encryption).
int email_prepare_file_message(const char* fileId, const char* fileName,
                                int64_t fileSize, const char* fileMd5,
                                int totalChunks, int chunkSize,
                                const char* text, const char* batchId,
                                char* outJson, int outSize);

// Prepare chunk message JSON (plaintext for encryption).
int email_prepare_truck_message(const char* fileId, int chunkIndex,
                                 const char* chunkDataB64, const char* chunkMd5,
                                 char* outJson, int outSize);

// Split a file into chunks and create send tasks for file metadata + each chunk.
// Returns 0 on success. outJson contains file_id and metadata.
int email_file_split_and_send(const char* filePath, const char* fileName,
                               const char* account, const char* recipient,
                               const char* sessionId, const char* inReplyTo,
                               const char* subject, const char* text,
                               const char* batchId,
                               char* outJson, int outSize);

// Multi-file variant: filesJson = [{"path":"...","name":"..."}, ...]. A single
// META task carries all files in a "files" array; chunks chain behind it.
int email_file_split_and_send_multi(const char* filesJson,
                               const char* account, const char* recipient,
                               const char* sessionId, const char* inReplyTo,
                               const char* subject, const char* text,
                               const char* batchId,
                               char* outJson, int outSize);

// Process a received "file" metadata message (create file_transfer record on receiver).
int email_file_transfer_receive_file(const char* fileId, const char* sessionId,
                                     const char* account, const char* sender,
                                     const char* fileName, int64_t fileSize,
                                     const char* fileMd5, int totalChunks, int chunkSize,
                                     const char* messageId,
                                     const char* compression, const char* compressedMd5,
                                     char* outJson, int outSize);

// Process a received "truck" chunk message (store chunk, auto-reassemble if complete).
int email_file_transfer_receive_truck(const char* fileId, int chunkIndex,
                                       const char* chunkDataB64, const char* chunkMd5,
                                       const char* outputDir,
                                       char* outJson, int outSize);

// Sender side: record that one chunk task (1.0.5 / 2.0.5) was delivered.
// taskBody is the task's stored body (truck JSON or MLS envelope with "plaintext").
int email_file_transfer_mark_chunk_sent(const char* taskBody);

// Query file transfer status by file_id.
int email_file_transfer_query(const char* fileId, char* outJson, int outSize);

// Query all pending file transfers for an account.
int email_file_transfer_query_pending(const char* account, char* outJson, int outSize);

// Reassemble a file from received chunks (manual trigger).
int email_file_transfer_reassemble(const char* fileId, const char* outputDir,
                                    char* outJson, int outSize);

// Copy a sent file from its original path to output directory (for sender Save As).
int email_file_transfer_copy_original(const char* fileId, const char* outputDir,
                                       char* outJson, int outSize);

// Addressbook API
int addressbook_add_email(const char* email, const char* name);
const char* addressbook_query_all();
const char* addressbook_query_groups();
int addressbook_update(int id, const char* name, const char* groupName, const char* notes);
int addressbook_delete(int id);
int addressbook_extract_from_header(const char* headerValue);
int addressbook_migrate_from_emails();

// --- Signal Protocol API (1:1, wire format v2) ---
// Keys: one long-lived identity key (IK) per account, one signed prekey (SPK) per
// session. Peers are trusted on first use; a changed peer IK is rejected.

// Generate a new session id (sig_xxx) with a fresh SPK and return
// {status, session_id, prekey_bundle:{ik_pub, spk_pub, spk_sig}}.
int signal_get_prekey_bundle(const char* account, char* outJson, int outSize);

// Same, for an existing session id (responder side of the bundle exchange).
int signal_get_prekey_bundle_for_session(const char* account, const char* sessionId,
                                         char* outJson, int outSize);

// Verify and store a peer's prekey bundle for a session.
// Returns 0 ok, -2 invalid SPK signature, -3 peer identity key changed, -1 bad args/db.
int signal_store_peer_prekey(const char* account, const char* peerEmail,
                             const char* sessionId, const char* ikPub,
                             const char* spkPub, const char* spkSig);

// Initiator: X3DH with the stored peer bundle for sessionId, Double Ratchet init,
// and the first encrypted message. outJson: {status, session_id, message}.
int signal_session_initiate_with_id(const char* account, const char* peerEmail,
                                    const char* sessionId,
                                    const char* plaintext, char* outJson, int outSize,
                                    const char* messageId,
                                    const char* inReplyTo);

// Encrypt a message in an existing session. outJson: {status, session_id, message}.
int signal_session_encrypt(const char* account, const char* peerEmail,
                           const char* sessionId, const char* plaintext,
                           char* outJson, int outSize,
                           const char* messageId,
                           const char* inReplyTo);

// Decrypt a received session_init / ratchet_msg body. A session_init creates the
// responder-side session. outJson: {status, plaintext, session_id, x_message_id, x_reply_to}
// or {status:"error", error}. Errors include identity_changed and bad_spk_signature.
int signal_session_decrypt(const char* account, const char* peerEmail,
                           const char* jsonBody, char* outJson, int outSize);

// 1 if a ratchet session exists for (account, peerEmail, sessionId), else 0.
int signal_session_exists(const char* account, const char* peerEmail,
                          const char* sessionId);

// Looks up signal_session_id from the email session table, then checks readiness.
int signal_session_exists_for_email_session(const char* account,
                                             const char* emailSessionId);

int signal_session_close(const char* account, const char* peerEmail,
                         const char* sessionId);

// Hex SHA-256 fingerprints for safety-number comparison. Empty string if unknown.
int signal_identity_fingerprint(const char* account, char* out, int outSize);
int signal_peer_identity_fingerprint(const char* account, const char* peerEmail,
                                     char* out, int outSize);

// Forget the trusted identity key of a peer (user confirmed the key change).
int signal_peer_identity_reset(const char* account, const char* peerEmail);

// === Group messaging (MLS / RFC 9420, X-Mailer 2.0.x) ===
// Implemented in signal/group_c_api.cpp on top of signal/mls_group.{h,cpp}.
// Fully independent from the 1:1 Double Ratchet channel.

// Create a group: inserts the group_session row, creates the MLS group and queues the
// root invite (2.0.0 / mls_invite) to every member. membersJson: JSON array of emails
// (may include the account itself). outJson: {status, group_id, root_message_id}.
int group_create(const char* account, const char* subject,
                 const char* membersJson, char* outJson, int outSize);

// Extended variant: xSessionId lets the group inherit an existing conversation's
// x_session_id (e.g. a 1:1 session's root x-message-id during a Signal->MLS upgrade),
// and inReplyTo becomes the invite's x_reply_to (last message id of that conversation).
// Pass NULL/"" for both to get the same behavior as group_create.
int group_create_ex(const char* account, const char* subject, const char* membersJson,
                    const char* xSessionId, const char* inReplyTo,
                    char* outJson, int outSize);

// Owner: invite additional members into an existing group (they answer with a
// KeyPackage and are added by the normal flow). outJson: {status, added, task_id}.
int group_invite_members(const char* account, const char* groupId, const char* membersJson,
                         char* outJson, int outSize);

// Get group info (members, epoch, ready). Returns 0 on success, negative on error.
int group_get_info(const char* account, const char* groupId,
                   char* outJson, int outSize);

// List all active groups for an account.
// outJson: {status, groups:[{group_id, group_email, x_reply_id, subject, owner, members, account, epoch, ready}]}
int group_list(const char* account, char* outJson, int outSize);

// Queue an application message (2.0.3). The task body holds the plaintext; the task
// processor MLS-encrypts it right before sending and keeps the plaintext in the local .eml.
// inReplyTo: local x_message_id of the last message of the group (chain parent).
// outJson: {status, task_id, message_id}. Returns 0 on success, negative on error.
int group_send_message(const char* account, const char* groupId,
                       const char* plaintext, const char* inReplyTo,
                       char* outJson, int outSize);

// Queue a file transfer to an MLS group: compress -> chunk -> one 2.0.4 meta task
// plus N 2.0.5 chunk tasks (MLS-encrypted per task right before sending).
// unifiedSessionId is the us_xxx id; the group's mls_group_id is resolved from it.
// inReplyTo: x_message_id of the conversation's last message. text: caption shown
// with the file card. outJson: {status, file_id, file_name, total_chunks, message_id}.
int group_send_file(const char* account, const char* unifiedSessionId,
                    const char* filePath, const char* fileName,
                    const char* inReplyTo, const char* subject,
                    const char* text, const char* batchId,
                    char* outJson, int outSize);

// Multi-file variant: filesJson = [{"path":"...","name":"..."}, ...]. One 2.0.4
// META task carries all files in a "files" array; 2.0.5 chunks chain behind it.
int group_send_file_multi(const char* account, const char* unifiedSessionId,
                    const char* filesJson, const char* inReplyTo,
                    const char* subject, const char* text, const char* batchId,
                    char* outJson, int outSize);

// Receive-side handler for any 2.0.x mail (called by download_pending).
// bodyText = JSON body; message_id/in_reply_to = local ids from the body (header fallback).
// On success writes the local group_id to outGroupId and returns 0 and, for 2.0.3,
// the decrypted plaintext to outPlaintext. Returns 1 if the message must be retried later
// (e.g. Commit before Welcome), negative on permanent failure.
int group_handle_incoming(const char* account, const char* fromAddr,
                          const char* xMailer, const char* bodyText,
                          const char* message_id, const char* in_reply_to,
                          char* outGroupId, int gidSize,
                          char* outPlaintext, int ptSize);

// Send-side hooks for the task processor.
// Before send: for 2.0.3 replace the plaintext body with the MLS ciphertext body.
// outBody receives the wire body. Returns 0 on success, negative on error.
int group_prepare_outgoing(const char* account, const char* xMailer,
                           const char* body, const char* inReplyTo,
                           char* outBody, int outSize);
// After send: resolve the group of a sent 2.0.x mail (by chain / root id) and
// associate the sent row; for 2.0.3 rewrite the local .eml body with `localBody`.
int group_after_sent(const char* account, const char* xMailer,
                     const char* messageId, const char* inReplyTo,
                     const char* localBody, const char* dataDir);

// ============================================================================
// Unified Session Manager (facade over Signal 1:1 + MLS 1:n)
// ============================================================================

// Create a new encrypted session (auto-selects Signal for 2 members, MLS for 3+).
// membersJson: JSON array of email addresses (must include the local account).
// pinned/hidden: 1=置顶/隐藏 at creation time.
// The handshake (PREKEY_BUNDLE / MLS invite) is queued in the outbox here.
// outJson: {status, session_id, message_id, x_mailer, task_id}
int us_create_session(const char* account, const char* subject,
                      const char* membersJson, const char* mailmenJson,
                      int pinned, int hidden,
                      char* outJson, int outSize);

// Send a message in an existing session (auto-routes to Signal or MLS). The body
// x-reply-to is resolved by the core (last x-message-id of the conversation).
// outJson: {status, session_id, message_id, x_mailer, task_id}
int us_send_message(const char* account, const char* sessionId,
                    const char* plaintext, char* outJson, int outSize);

// Queue files with a caption. filesJson: [{"path":"...","name":"..."}].
// outJson: {status, message_id, files:[{file_id,file_name,file_size,total_chunks}],
//           batch_id, x_mailer}
int us_send_files(const char* account, const char* sessionId,
                  const char* filesJson, const char* text,
                  char* outJson, int outSize);

// Displayable messages of a session, strictly by the body x-reply-to chain from the
// session root, in reply-chain order. Returns 0, or -(needed size) when outSize is
// too small. outJson: {status, messages:[{rowid, x_message_id, x_reply_to, sender,
// timestamp, is_sent, x_mailer, kind:"text"|"handshake"|"file", text, batch_id,
// files:[{file_id,file_name,file_size,total_chunks,received_chunks,transfer_status}]}]}
int us_list_messages(const char* account, const char* sessionId,
                     char* outJson, int outSize);

// Add members to a session (triggers irreversible upgrade to MLS if Signal + >2 total).
// outJson: {status, session_id, upgraded, mode, task_id}
int us_add_members(const char* account, const char* sessionId,
                   const char* membersJson, char* outJson, int outSize);

// List all active sessions for an account.
// outJson: {status, sessions: [{session_id, mode, members, ...}]}
int us_list_sessions(const char* account, char* outJson, int outSize);

// Get a specific session by its unified session_id.
// outJson: {status, session: {session_id, mode, members, ...}}
int us_get_session(const char* sessionId, char* outJson, int outSize);

// Update the pinned/hidden display flags of a session.
// outJson: {status}
int us_set_session_flags(const char* sessionId, int pinned, int hidden,
                         char* outJson, int outSize);

// Update the subject (title) of a session.
// outJson: {status}
int us_set_subject(const char* sessionId, const char* subject,
                   char* outJson, int outSize);

// Check if a session is ready for sending. Returns 1 if ready, 0 if not, negative on error.
int us_is_ready(const char* account, const char* sessionId);

// Signal sessions flagged identity_changed (status 2): drop the pinned peer key
// and reopen the session after the user has verified the new fingerprint.
int us_reset_peer_identity(const char* sessionId, char* outJson, int outSize);

#ifdef __cplusplus
}
#endif

#endif // EMAIL_CORE_H
