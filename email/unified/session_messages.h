#ifndef UNIFIED_SESSION_MESSAGES_H
#define UNIFIED_SESSION_MESSAGES_H

#include "unified_session.h"
#include <nlohmann/json.hpp>
#include <string>

// Conversation reconstruction for a unified session.
//
// Membership is strictly the body x-message-id / x-reply-to chain: a message belongs
// to the session iff following x-reply-to from it reaches the session's root
// x-message-id. Header Message-ID / In-Reply-To are never consulted.

// Displayable messages of the session in reply-chain order (every reply below its
// parent, siblings by arrival). Each entry:
//   {rowid, x_message_id, x_reply_to, sender, timestamp, is_sent, x_mailer,
//    kind: "text"|"handshake"|"file", text, batch_id, files:[{file_id, file_name,
//    file_size, total_chunks, received_chunks, transfer_status}]}
nlohmann::json session_list_messages(const UnifiedSession& us);

// x-message-id the next outgoing message must reply to: the last message of the
// conversation, continued through any still-queued outbox tasks.
std::string session_last_message_id(const UnifiedSession& us);

#endif // UNIFIED_SESSION_MESSAGES_H
