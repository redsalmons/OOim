#include "unified_session_manager.h"
#include "email_core.h"
#include "email_core_common.h"
#include "logger.h"
#include "x_mailer.h"
#include "task_repo.h"
#include "mls_group.h"
#include "persistence/signal_session_repo.h"
#include "persistence/session_repo.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <random>
#include <ctime>

using json = nlohmann::json;

// ---------- helpers ----------

std::string UnifiedSessionManager::generateSessionId() {
    static std::mt19937_64 rng{std::random_device{}()};
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    return "us_" + std::to_string(secs) + "." + std::to_string(rng() % 100000000);
}

std::string UnifiedSessionManager::generateMessageId(const std::string& account) {
    static std::mt19937_64 rng{std::random_device{}()};
    std::string domain = account.substr(account.find('@') + 1);
    if (domain.empty()) domain = "oim";
    return "<" + std::to_string(std::time(nullptr)) + "." +
           std::to_string(rng() % 100000000) + "@" + domain + ">";
}

static std::string joinRecipients(const std::vector<std::string>& v,
                                   const std::string& exclude) {
    std::string out;
    for (auto& m : v) {
        if (m == exclude) continue;
        if (!out.empty()) out += ", ";
        out += m;
    }
    return out;
}

// ---------- createSession ----------

SendResult UnifiedSessionManager::createSession(const std::string& account,
                                                 const std::string& subject,
                                                 const std::vector<std::string>& members) {
    SendResult result;

    if (members.size() < 2) {
        result.error = "need at least 2 members (including self)";
        return result;
    }

    // Ensure the local account is in the member list.
    std::vector<std::string> allMembers = members;
    if (std::find(allMembers.begin(), allMembers.end(), account) == allMembers.end()) {
        allMembers.insert(allMembers.begin(), account);
    }

    if (allMembers.size() == 2) {
        // 1:1 → Signal
        result = createSignalSession_(account, subject, allMembers);
    } else {
        // 1:n → MLS
        result = createMlsSession_(account, subject, allMembers);
    }

    return result;
}

// ---------- createSignalSession_ ----------

SendResult UnifiedSessionManager::createSignalSession_(const std::string& account,
                                                        const std::string& subject,
                                                        const std::vector<std::string>& members) {
    SendResult result;

    // Find the peer (the one that is not the local account).
    std::string peer;
    for (auto& m : members) {
        if (m != account) { peer = m; break; }
    }
    if (peer.empty()) {
        result.error = "cannot determine peer";
        return result;
    }

    // Generate unified session id.
    std::string usId = generateSessionId();
    std::string msgId = generateMessageId(account);

    // NOTE: We do NOT call signal_session_initiate() here.
    // The SMTP layer (sendViaConfigRaw) handles Signal session initiation
    // and encryption when it sees encrypt_method=1 and session_id=''.
    // This avoids double-initiation conflicts.

    // Persist unified session with empty signal_session_id.
    UnifiedSession rec;
    rec.sessionId = usId;
    rec.account = account;
    rec.subject = subject;
    rec.mode = SessionMode::Signal;
    rec.members = members;
    rec.signalSessionId = "";  // will be filled after first SMTP send
    rec.rootMessageId = msgId;
    rec.status = 0;
    if (!repo_.create(rec)) {
        result.error = "failed to persist unified session";
        return result;
    }

    result.success = true;
    result.sessionId = usId;
    result.messageId = msgId;
    result.xMailer = XMailer::SESSION_INIT;
    result.encryptedBody = "";  // SMTP layer will encrypt
    return result;
}

// ---------- createMlsSession_ ----------

SendResult UnifiedSessionManager::createMlsSession_(const std::string& account,
                                                     const std::string& subject,
                                                     const std::vector<std::string>& members) {
    SendResult result;

    std::string usId = generateSessionId();
    std::string rootMsgId = generateMessageId(account);

    // Create the MLS group via the existing API.
    // We pass the members as a JSON array string.
    json membersJson = json::array();
    for (auto& m : members) {
        if (m != account) membersJson.push_back(m);
    }

    char groupBuf[65536];
    int rc = group_create(account.c_str(), subject.c_str(),
                          membersJson.dump().c_str(), groupBuf, sizeof(groupBuf));
    if (rc != 0) {
        result.error = "group_create failed";
        LOG_INFO("[USMgr] group_create failed rc=%d\n", rc);
        return result;
    }

    // Parse group_id from response.
    std::string groupId;
    int64_t taskId = 0;
    try {
        auto resp = json::parse(groupBuf);
        groupId = resp.value("group_id", "");
        taskId = resp.value("task_id", (int64_t)0);
    } catch (...) {}

    // Persist unified session.
    UnifiedSession rec;
    rec.sessionId = usId;
    rec.account = account;
    rec.subject = subject;
    rec.mode = SessionMode::Mls;
    rec.members = members;
    rec.mlsGroupId = groupId;
    rec.rootMessageId = rootMsgId;
    rec.status = 0;
    if (!repo_.create(rec)) {
        result.error = "failed to persist unified session";
        return result;
    }

    result.success = true;
    result.sessionId = usId;
    result.messageId = rootMsgId;
    result.xMailer = XMailer::MLS_KEY_PACKAGE;
    result.taskId = taskId;
    return result;
}

// ---------- sendMessage ----------

SendResult UnifiedSessionManager::sendMessage(const std::string& account,
                                               const std::string& sessionId,
                                               const std::string& plaintext,
                                               const std::string& inReplyTo) {
    SendResult result;

    UnifiedSession session;
    if (!repo_.load(sessionId, session)) {
        result.error = "session not found: " + sessionId;
        return result;
    }

    std::string msgId = generateMessageId(account);

    if (session.mode == SessionMode::Signal) {
        // Find the peer.
        std::string peer;
        for (auto& m : session.members) {
            if (m != account) { peer = m; break; }
        }
        if (peer.empty()) {
            result.error = "cannot determine peer in Signal session";
            return result;
        }

        // If signalSessionId is empty (first send after creation), look it up
        // from the Signal session table (SMTP layer established it during send).
        std::string sigSid = session.signalSessionId;
        if (sigSid.empty()) {
            static SignalSessionRepo s_sigRepo;
            SignalSessionRecord activeRec;
            if (s_sigRepo.loadActiveSession(account, peer, activeRec)) {
                sigSid = activeRec.sessionId;
                // Persist for future calls
                repo_.updateSignalSessionId(sessionId, sigSid);
                session.signalSessionId = sigSid;
                LOG_INFO("[USMgr] sendMessage: auto-resolved signalSessionId=%s for us=%s\n",
                         sigSid.c_str(), sessionId.c_str());
            } else {
                result.error = "signal session not yet established (no active session found)";
                return result;
            }
        }

        // Encrypt via Signal Double Ratchet. This happens once, right here: the
        // ciphertext is enqueued in the outbox and every retry resends it verbatim
        // (re-encrypting would advance the ratchet again and desync the peer).
        char sigBuf[65536];
        int rc = signal_session_encrypt(account.c_str(), peer.c_str(),
                                        sigSid.c_str(),
                                        plaintext.c_str(),
                                        sigBuf, sizeof(sigBuf),
                                        msgId.c_str(), inReplyTo.c_str());
        if (rc != 0) {
            result.error = "signal_session_encrypt failed";
            return result;
        }

        std::string encryptedBody;
        try {
            auto resp = json::parse(sigBuf);
            if (resp.contains("message")) {
                encryptedBody = resp["message"].dump();
            }
        } catch (...) {}
        if (encryptedBody.empty()) {
            result.error = "signal_session_encrypt returned no message";
            return result;
        }

        // Outbox: persist before sending. The task body holds the ciphertext
        // (pre_encrypted=1), local_body holds the plaintext for the local archive,
        // and the per-account background loop delivers it with retry/backoff.
        static TaskRepo s_outboxRepo;
        int64_t taskId = s_outboxRepo.insert(account, peer, session.subject,
                                             encryptedBody, inReplyTo, msgId,
                                             msgId, sessionId, XMailer::RATCHET_MSG,
                                             1, plaintext);
        if (taskId == 0) {
            result.error = "failed to enqueue task";
            return result;
        }

        result.success = true;
        result.taskId = taskId;
        result.sessionId = sessionId;
        result.messageId = msgId;
        result.xMailer = XMailer::RATCHET_MSG;
        result.encryptedBody = encryptedBody;
        LOG_INFO("[USMgr] sendMessage: enqueued outbox task=%lld for us=%s msg=%s\n",
                 (long long)taskId, sessionId.c_str(), msgId.c_str());

    } else {
        // MLS mode: queue via group_send_message.
        char groupBuf[65536];
        int rc = group_send_message(account.c_str(), session.mlsGroupId.c_str(),
                                    plaintext.c_str(), inReplyTo.c_str(),
                                    groupBuf, sizeof(groupBuf));
        if (rc != 0) {
            result.error = "group_send_message failed";
            return result;
        }

        try {
            auto resp = json::parse(groupBuf);
            result.taskId = resp.value("task_id", (int64_t)0);
            result.messageId = resp.value("message_id", msgId);
        } catch (...) {}

        result.success = true;
        result.sessionId = sessionId;
        result.xMailer = XMailer::MLS_APP_MSG;
    }

    return result;
}

// ---------- handleIncoming ----------

RecvResult UnifiedSessionManager::handleIncoming(const std::string& account,
                                                  const std::string& from,
                                                  const std::string& xMailer,
                                                  const std::string& body,
                                                  const std::string& messageId,
                                                  const std::string& inReplyTo) {
    RecvResult result;

    // Determine protocol from X-Mailer.
    bool isSignal = (xMailer == XMailer::PREKEY_BUNDLE ||
                     xMailer == XMailer::SESSION_INIT ||
                     xMailer == XMailer::RATCHET_MSG ||
                     xMailer == XMailer::REPAIR_MSG ||
                     xMailer == XMailer::ATTACH_META ||
                     xMailer == XMailer::ATTACH_CHUNK);

    bool isMls = XMailer::isMls(xMailer);

    if (isSignal) {
        // Route to Signal decrypt.
        char sigBuf[65536];
        int rc = signal_session_decrypt(account.c_str(), from.c_str(),
                                        body.c_str(), sigBuf, sizeof(sigBuf));
        if (rc != 0) {
            result.error = "signal_session_decrypt failed";
            return result;
        }

        try {
            auto resp = json::parse(sigBuf);
            result.success = resp.value("status", "") == "success";
            result.plaintext = resp.value("plaintext", "");
            result.sessionId = resp.value("session_id", "");
            result.xMailer = xMailer;
        } catch (...) {
            result.error = "failed to parse signal decrypt response";
        }

    } else if (isMls) {
        // Route to MLS group handler.
        char gidBuf[128];
        char ptBuf[65536];
        int rc = group_handle_incoming(account.c_str(), from.c_str(),
                                       xMailer.c_str(), body.c_str(),
                                       messageId.c_str(), inReplyTo.c_str(),
                                       gidBuf, sizeof(gidBuf),
                                       ptBuf, sizeof(ptBuf));
        if (rc != 0) {
            result.error = "group_handle_incoming failed rc=" + std::to_string(rc);
            return result;
        }

        result.success = true;
        result.plaintext = ptBuf;
        result.xMailer = xMailer;
        // Try to find the unified session by mls_group_id.
        // (For now, we leave sessionId empty; the caller can resolve it.)
    } else {
        result.error = "unknown X-Mailer: " + xMailer;
    }

    return result;
}

// ---------- addMembers ----------

SendResult UnifiedSessionManager::addMembers(const std::string& account,
                                              const std::string& sessionId,
                                              const std::vector<std::string>& newMembers,
                                              const std::string& inReplyTo) {
    SendResult result;

    UnifiedSession session;
    if (!repo_.load(sessionId, session)) {
        result.error = "session not found";
        return result;
    }

    // Merge member lists (deduplicate).
    std::vector<std::string> allMembers = session.members;
    for (auto& m : newMembers) {
        if (std::find(allMembers.begin(), allMembers.end(), m) == allMembers.end()) {
            allMembers.push_back(m);
        }
    }

    if (session.mode == SessionMode::Signal && allMembers.size() > 2) {
        // Upgrade to MLS (irreversible).
        result = upgradeToMls_(account, session, newMembers, inReplyTo);
    } else if (session.mode == SessionMode::Mls) {
        // Normal MLS addMembers.
        json kpJson = json::array();
        for (auto& m : newMembers) {
            // We need their KeyPackages; for now, queue invites.
            (void)m; // KeyPackages will arrive via the invite flow.
        }

        // Update the member list in the unified session.
        repo_.updateMembers(sessionId, allMembers);

        result.success = true;
        result.sessionId = sessionId;
        result.xMailer = XMailer::MLS_KEY_PACKAGE;
    } else {
        result.error = "cannot add members to a 2-member Signal session without upgrading";
    }

    return result;
}

// ---------- upgradeToMls_ ----------

SendResult UnifiedSessionManager::upgradeToMls_(const std::string& account,
                                                 UnifiedSession& existingSession,
                                                 const std::vector<std::string>& additionalMembers,
                                                 const std::string& inReplyTo) {
    SendResult result;

    LOG_INFO("[USMgr] upgrading session=%s from Signal to MLS\n",
             existingSession.sessionId.c_str());

    // Build the full member list.
    std::vector<std::string> allMembers = existingSession.members;
    for (auto& m : additionalMembers) {
        if (std::find(allMembers.begin(), allMembers.end(), m) == allMembers.end()) {
            allMembers.push_back(m);
        }
    }

    // Create a new MLS group. The group's x_session_id inherits the existing
    // conversation's root message id so receivers associate the invite with the
    // same conversation (1:1 -> group continuation, no new session).
    json membersJson = json::array();
    for (auto& m : allMembers) {
        if (m != account) membersJson.push_back(m);
    }

    char groupBuf[65536];
    int rc = group_create_ex(account.c_str(), existingSession.subject.c_str(),
                             membersJson.dump().c_str(), existingSession.rootMessageId.c_str(),
                             inReplyTo.c_str(), groupBuf, sizeof(groupBuf));
    if (rc != 0) {
        result.error = "group_create failed during upgrade";
        return result;
    }

    std::string groupId;
    int64_t taskId = 0;
    try {
        auto resp = json::parse(groupBuf);
        groupId = resp.value("group_id", "");
        taskId = resp.value("task_id", (int64_t)0);
    } catch (...) {}

    // Update the unified session: mode → mls, set group id and members.
    if (!repo_.upgradeToMls(existingSession.sessionId, groupId, allMembers)) {
        result.error = "failed to update session mode to MLS";
        return result;
    }

    // Note: The old Signal session keys are preserved in the DB for decrypting
    // historical messages, but the session is marked as closed in the unified table.

    result.success = true;
    result.sessionId = existingSession.sessionId;
    result.xMailer = XMailer::MLS_KEY_PACKAGE;
    result.taskId = taskId;
    result.upgraded = true;
    return result;
}

// ---------- signalEncrypt ----------

bool UnifiedSessionManager::signalEncrypt(const std::string& account, const std::string& sessionId,
                                          const std::string& plaintext, const std::string& messageId,
                                          const std::string& inReplyTo,
                                          std::string& outEnvelope, std::string& error) {
    UnifiedSession session;
    if (!repo_.load(sessionId, session)) { error = "session not found"; return false; }
    if (session.mode != SessionMode::Signal) { error = "not a Signal session"; return false; }

    std::string peer;
    for (auto& m : session.members) if (m != account) { peer = m; break; }
    if (peer.empty()) { error = "cannot determine peer"; return false; }

    std::string sigSid = session.signalSessionId;
    if (sigSid.empty()) {
        static SignalSessionRepo s_sigRepo;
        SignalSessionRecord activeRec;
        if (!s_sigRepo.loadActiveSession(account, peer, activeRec)) {
            error = "signal session not yet established";
            return false;
        }
        sigSid = activeRec.sessionId;
        repo_.updateSignalSessionId(sessionId, sigSid);
    }

    // Envelope = plaintext (base64 ciphertext grows ~4/3) + headers/keys; size generously.
    std::vector<char> buf(plaintext.size() * 2 + 65536);
    int rc = signal_session_encrypt(account.c_str(), peer.c_str(), sigSid.c_str(),
                                    plaintext.c_str(), buf.data(), (int)buf.size(),
                                    messageId.c_str(), inReplyTo.c_str());
    if (rc != 0) { error = "signal_session_encrypt failed"; return false; }

    try {
        auto resp = json::parse(buf.data());
        if (resp.value("status", "") != "success" || !resp.contains("message")) {
            error = resp.value("error", "encrypt error");
            return false;
        }
        outEnvelope = resp["message"].dump();
    } catch (const std::exception& e) {
        error = std::string("parse error: ") + e.what();
        return false;
    }
    return true;
}

// ---------- query methods ----------

std::vector<UnifiedSession> UnifiedSessionManager::listSessions(const std::string& account) {
    return repo_.listByAccount(account);
}

UnifiedSession UnifiedSessionManager::getSession(const std::string& sessionId) {
    UnifiedSession session;
    repo_.load(sessionId, session);
    return session;
}

bool UnifiedSessionManager::isReady(const std::string& account, const std::string& sessionId) {
    UnifiedSession session;
    if (!repo_.load(sessionId, session)) return false;

    if (session.mode == SessionMode::Signal) {
        // The peer identifies the Signal session row: signal_session records are keyed
        // by (account, peer_email, session_id), so the peer must be passed through to
        // the existence check — an empty peer never matches any row.
        std::string peer;
        for (auto& m : session.members) {
            if (m != account) { peer = m; break; }
        }
        if (peer.empty()) return false;

        // If signalSessionId is empty, try to auto-resolve from Signal session table.
        std::string sigSid = session.signalSessionId;
        if (sigSid.empty()) {
            static SignalSessionRepo s_sigRepo;
            SignalSessionRecord activeRec;
            if (s_sigRepo.loadActiveSession(account, peer, activeRec)) {
                sigSid = activeRec.sessionId;
                repo_.updateSignalSessionId(sessionId, sigSid);
            }
        }
        if (sigSid.empty()) return false;
        if (signal_session_exists(account.c_str(), peer.c_str(), sigSid.c_str()) != 1) return false;

        // A signal_session row is written the instant we *initiate* X3DH, so its mere
        // existence only proves we hold the peer's keys. Sending is allowed once the
        // peer has proven it holds ours too (its PREKEY_BUNDLE processed, or our bundle
        // delivered as responder) — that is what kex_done records.
        static SignalSessionRepo s_kexRepo;
        return s_kexRepo.isKexDone(account, sigSid);
    } else {
        // MLS: every member other than ourselves must already be in the ratchet tree.
        // Right after creation, or while a 1:1 is being upgraded to 1:n, the tree still
        // holds a subset, so those members' keys are not exchanged yet and sending must
        // stay blocked until the last one joins.
        if (!mls::isReady(account, session.mlsGroupId)) return false;
        std::vector<std::string> tree = mls::members(account, session.mlsGroupId);
        size_t peers = 0;
        for (auto& m : session.members) {
            if (m == account || m.empty()) continue;
            peers++;
            if (std::find(tree.begin(), tree.end(), m) == tree.end()) return false;
        }
        return peers > 0;
    }
}
