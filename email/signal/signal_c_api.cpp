#include "email_core.h"
#include "email_core_common.h"
#include "signal_protocol.h"
#include "signal_message.h"
#include "signal_key_repo.h"
#include "signal_session_repo.h"
#include "session_repo.h"
#include "logger.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <cstdint>

using json = nlohmann::json;

static SignalKeyRepo s_signalKeyRepo;
static SignalSessionRepo s_signalSessionRepo;
static SessionRepo s_sessionRepo;

extern "C" int signal_init_account(const char* account) {
    if (!account) return -1;
    std::string acc(account);

    if (!ensure_identity_key(acc)) return -2;
    if (!ensure_signed_prekey(acc)) return -3;
    if (!ensure_one_time_prekeys(acc, 5)) return -4;

    LOG_INFO("[Signal] signal_init_account: initialized for %s\n", acc.c_str());
    return 0;
}

extern "C" int signal_get_prekey_bundle(const char* account, char* outJson, int outSize) {
    if (!account || !outJson || outSize <= 0) return -1;
    std::string acc(account);

    // Generate sig_xxx for per-session keys
    std::string sigId = generate_session_id();

    // Provision per-session keys for this sig_xxx
    if (!ensure_identity_key_for_session(sigId)) {
        snprintf(outJson, outSize, R"({"status":"error","error":"no_identity_key"})");
        return -2;
    }
    if (!ensure_signed_prekey_for_session(sigId)) {
        snprintf(outJson, outSize, R"({"status":"error","error":"no_signed_prekey"})");
        return -3;
    }

    PrekeyBundle bundle = build_prekey_bundle_for_session(sigId);
    if (bundle.ikPub.empty()) {
        if (outJson && outSize > 0) snprintf(outJson, outSize, R"({"status":"error","error":"no_identity_key"})");
        return -2;
    }

    json j;
    j["status"] = "success";
    j["session_id"] = sigId;
    j["prekey_bundle"] = {
        {"ik_pub", bundle.ikPub},
        {"spk_pub", bundle.spkPub},
        {"spk_sig", bundle.spkSig},
        {"opk_pub", bundle.opkPub}
    };

    std::string s = j.dump();
    snprintf(outJson, outSize, "%s", s.c_str());
    LOG_INFO("[Signal] signal_get_prekey_bundle: generated bundle with session_id=%s for %s\n", sigId.c_str(), acc.c_str());
    return 0;
}

extern "C" int signal_session_initiate(const char* account, const char* peerEmail,
                                       const char* plaintext, char* outJson, int outSize,
                                       const char* messageId,
                                       const char* inReplyTo) {
    if (!account || !peerEmail || !plaintext || !outJson || outSize <= 0) return -1;

    std::string acc(account);
    std::string peer(peerEmail);
    std::string text(plaintext);

    // Get peer's prekey bundle from cache
    SignalPeerPrekeyRecord peerRec;
    if (!s_signalKeyRepo.loadPeerPrekey(acc, peer, peerRec)) {
        snprintf(outJson, outSize, R"({"status":"error","error":"no_peer_prekey"})");
        LOG_INFO("[Signal] signal_session_initiate: no peer prekey for %s\n", peer.c_str());
        return -4;
    }

    // Generate a new sig_xxx for this new session (do NOT reuse peer prekey's sig_xxx)
    std::string sessionId = generate_session_id();
    LOG_INFO("[Signal] signal_session_initiate: generated new session_id=%s for %s -> %s\n",
             sessionId.c_str(), acc.c_str(), peer.c_str());

    // Re-store peer prekey under the new sig_xxx so per-session key lookup works
    s_signalKeyRepo.upsertPeerPrekey(acc, peer,
        peerRec.ikPub, peerRec.spkPub, peerRec.spkSig, peerRec.opkPub, sessionId);

    // Provision our per-session keys for this new sig_xxx
    if (!ensure_identity_key_for_session(sessionId)) {
        snprintf(outJson, outSize, R"({"status":"error","error":"no_identity_key"})");
        return -2;
    }
    if (!ensure_signed_prekey_for_session(sessionId)) {
        snprintf(outJson, outSize, R"({"status":"error","error":"no_signed_prekey"})");
        return -3;
    }

    PrekeyBundle peerBundle;
    peerBundle.ikPub = peerRec.ikPub;
    peerBundle.spkPub = peerRec.spkPub;
    peerBundle.spkSig = peerRec.spkSig;
    peerBundle.opkPub = peerRec.opkPub;

    // X3DH: compute root key using our per-session IK
    std::string rootKey, ekPub, ekPriv, ekPassword;
    if (!x3dh_initiate_for_session(sessionId, peerBundle, rootKey, ekPub, ekPriv, ekPassword)) {
        snprintf(outJson, outSize, R"({"status":"error","error":"x3dh_failed"})");
        return -6;
    }

    // Initialize Double Ratchet (as initiator) using per-session keys
    // The initial peer DH is the peer's signed prekey
    if (!dr_initialize_for_session(sessionId, acc, peer, sessionId, rootKey, peerBundle.spkPub, true)) {
        snprintf(outJson, outSize, R"({"status":"error","error":"dr_init_failed"})");
        return -7;
    }

    // Encrypt first message
    EncryptedMessage encMsg;
    if (!dr_encrypt(acc, peer, sessionId, text, encMsg)) {
        snprintf(outJson, outSize, R"({"status":"error","error":"dr_encrypt_failed"})");
        return -8;
    }

    // Build our own per-session prekey bundle to include in the init message
    PrekeyBundle myBundle = build_prekey_bundle_for_session(sessionId);

    // Encode as SESSION_INIT message
    std::vector<std::string> recipients = {peer};
    json msgJson = signal_msg::encode_session_init(
        sessionId, acc, recipients, encMsg,
        myBundle.ikPub, ekPub, myBundle,
        messageId ? messageId : "",
        inReplyTo ? inReplyTo : "",
        peerBundle.spkPub,  // peer's SPK pub that we used (so responder can find matching priv)
        peerBundle.ikPub   // peer's IK pub that we used (so responder can find matching priv)
    );

    json result;
    result["status"] = "success";
    result["session_id"] = sessionId;
    result["message"] = msgJson;

    std::string s = result.dump();
    snprintf(outJson, outSize, "%s", s.c_str());

    LOG_INFO("[Signal] signal_session_initiate: session %s created for %s -> %s\n",
             sessionId.c_str(), acc.c_str(), peer.c_str());
    return 0;
}

extern "C" int signal_session_encrypt(const char* account, const char* peerEmail,
                                      const char* sessionId, const char* plaintext,
                                      char* outJson, int outSize,
                                      const char* messageId,
                                      const char* inReplyTo) {
    if (!account || !peerEmail || !sessionId || !plaintext || !outJson || outSize <= 0) return -1;

    std::string acc(account);
    std::string peer(peerEmail);
    std::string sid(sessionId ? sessionId : "");
    std::string text(plaintext);

    // If sessionId is empty, return error — per-session design requires explicit sig_xxx
    if (sid.empty()) {
        snprintf(outJson, outSize, R"({"status":"error","error":"no_session_id_provided"})");
        LOG_INFO("[Signal] signal_session_encrypt: empty sessionId for %s -> %s, refusing to use legacy per-account session\n",
                 acc.c_str(), peer.c_str());
        return -2;
    }

    EncryptedMessage encMsg;
    if (!dr_encrypt(acc, peer, sid, text, encMsg)) {
        snprintf(outJson, outSize, R"({"status":"error","error":"encrypt_failed"})");
        return -3;
    }

    std::vector<std::string> recipients = {peer};
    json msgJson = signal_msg::encode_ratchet_msg(sid, acc, recipients, encMsg,
        messageId ? messageId : "",
        inReplyTo ? inReplyTo : "");

    json result;
    result["status"] = "success";
    result["session_id"] = sid;
    result["message"] = msgJson;

    std::string s = result.dump();
    snprintf(outJson, outSize, "%s", s.c_str());
    return 0;
}

extern "C" int signal_session_decrypt(const char* account, const char* peerEmail,
                                      const char* jsonBody, char* outJson, int outSize) {
    if (!account || !peerEmail || !jsonBody || !outJson || outSize <= 0) return -1;

    std::string acc(account);
    std::string peer(peerEmail);

    // Parse the message
    signal_msg::ParsedSignalMessage parsed;
    if (!signal_msg::decode_signal_message_str(jsonBody, parsed)) {
        snprintf(outJson, outSize, R"({"status":"error","error":"parse_failed"})");
        return -2;
    }

    // If this is a SESSION_INIT, we need to create the session first
    if (parsed.isInit) {
        // Use parsed.sessionId (sig_xxx) as the per-session key scope
        const std::string& sigId = parsed.sessionId;

        // Provision our per-session keys for this sig_xxx
        // Look up our IK that the initiator actually used (by pub key from init message).
        // Same rationale as SPK: the initiator used our previously-published IK, NOT a freshly
        // generated one. If we generate a new IK here, the X3DH DH2 won't match.
        if (!parsed.peerIkPub.empty()) {
            SignalIdentityRecord matchedIk;
            if (s_signalKeyRepo.loadIdentityByPub(parsed.peerIkPub, matchedIk)) {
                // Re-store the matched IK under this sigId so per-session lookups find it
                s_signalKeyRepo.upsertIdentityForSession(sigId, matchedIk.ikPub,
                    matchedIk.ikPriv, matchedIk.ikPassword);
                LOG_INFO("[Signal] signal_session_decrypt: reused IK by pub match for session %s\n", sigId.c_str());
            } else {
                // IK not found; fall back to generating new
                LOG_INFO("[Signal] signal_session_decrypt: IK not found by pub, generating new for %s\n", sigId.c_str());
                if (!ensure_identity_key_for_session(sigId)) {
                    snprintf(outJson, outSize, R"({"status":"error","error":"no_identity_key"})");
                    return -3;
                }
            }
        } else {
            // Backward compat: no peer_ik_pub in message, generate new IK
            if (!ensure_identity_key_for_session(sigId)) {
                snprintf(outJson, outSize, R"({"status":"error","error":"no_identity_key"})");
                return -3;
            }
        }

        // Look up our SPK that the initiator actually used (by pub key from init message).
        // This is critical: the initiator used our previously-published SPK, NOT a freshly
        // generated one. If we generate a new SPK here, the DH shared secret won't match.
        if (!parsed.peerSpkPub.empty()) {
            SignalPrekeyRecord matchedSpk;
            if (s_signalKeyRepo.loadSignedPrekeyByPub(parsed.peerSpkPub, matchedSpk)) {
                // Re-store the matched SPK under this sigId so per-session lookups find it
                s_signalKeyRepo.insertSignedPrekeyForSession(sigId, matchedSpk.pub,
                    matchedSpk.priv, matchedSpk.password, matchedSpk.signature);
                LOG_INFO("[Signal] signal_session_decrypt: reused SPK by pub match for session %s\n", sigId.c_str());
            } else {
                // SPK not found (may have been rotated/lost); fall back to generating new
                LOG_INFO("[Signal] signal_session_decrypt: SPK not found by pub, generating new for %s\n", sigId.c_str());
                if (!ensure_signed_prekey_for_session(sigId)) {
                    snprintf(outJson, outSize, R"({"status":"error","error":"no_signed_prekey"})");
                    return -4;
                }
            }
        } else {
            // Backward compat: no peer_spk_pub in message, generate new SPK
            if (!ensure_signed_prekey_for_session(sigId)) {
                snprintf(outJson, outSize, R"({"status":"error","error":"no_signed_prekey"})");
                return -4;
            }
        }

        // Store peer's prekey bundle from the init message (scoped to sig_xxx)
        if (parsed.hasPrekeyBundle) {
            s_signalKeyRepo.upsertPeerPrekey(acc, peer,
                parsed.senderBundle.ikPub, parsed.senderBundle.spkPub,
                parsed.senderBundle.spkSig, parsed.senderBundle.opkPub, sigId);
        }

        // X3DH respond: compute root key using our per-session IK/SPK
        std::string rootKey;
        if (!x3dh_respond_for_session(sigId, parsed.ikPub, parsed.ekPub, rootKey)) {
            snprintf(outJson, outSize, R"({"status":"error","error":"x3dh_respond_failed"})");
            return -5;
        }

        // Initialize Double Ratchet as responder using per-session SPK
        if (!dr_initialize_for_session(sigId, acc, peer, parsed.sessionId, rootKey, parsed.header.dhPub, false)) {
            snprintf(outJson, outSize, R"({"status":"error","error":"dr_init_failed"})");
            return -6;
        }
    }

    // Build EncryptedMessage from parsed data
    EncryptedMessage encMsg;
    encMsg.header = parsed.header;
    encMsg.ciphertext = parsed.ciphertext;
    encMsg.ekPub = parsed.ekPub;
    encMsg.ikPub = parsed.ikPub;

    // Decrypt
    DecryptResult result = dr_decrypt(acc, peer, parsed.sessionId, encMsg);

    json j;
    if (result.success) {
        j["status"] = "success";
        j["plaintext"] = result.plaintext;
        j["session_id"] = result.sessionId;
        j["x_message_id"] = parsed.messageId;
        j["x_reply_to"] = parsed.inReplyTo;
    } else {
        j["status"] = "error";
        j["error"] = result.error;
        j["session_id"] = result.sessionId;
        j["x_message_id"] = parsed.messageId;
        j["x_reply_to"] = parsed.inReplyTo;
    }

    std::string s = j.dump();
    snprintf(outJson, outSize, "%s", s.c_str());

    LOG_INFO("[Signal] signal_session_decrypt: from=%s, session=%s, success=%d\n",
             peer.c_str(), parsed.sessionId.c_str(), result.success ? 1 : 0);
    return result.success ? 0 : -5;
}

extern "C" int signal_session_exists(const char* account, const char* peerEmail,
                                     const char* sessionId) {
    if (!account || !peerEmail) return -1;
    std::string sid(sessionId ? sessionId : "");

    if (!sid.empty()) {
        // Per-session check: look up specific sig_xxx
        SignalSessionRecord rec;
        if (s_signalSessionRepo.loadSession(account, peerEmail, sid, rec)) {
            return 1;
        }
        return 0;
    }

    // Fallback: check any session for (account, peer)
    SignalSessionRecord rec;
    if (s_signalSessionRepo.loadActiveSession(account, peerEmail, rec)) {
        return 1;
    }
    return 0;
}

extern "C" int signal_session_close(const char* account, const char* peerEmail,
                                    const char* sessionId) {
    if (!account || !peerEmail || !sessionId) return -1;
    return s_signalSessionRepo.closeSession(account, peerEmail, sessionId) ? 0 : -1;
}

extern "C" int signal_session_exists_for_email_session(const char* account,
                                                         const char* emailSessionId) {
    if (!account || !emailSessionId) return -1;
    std::string acc(account);
    std::string emailSid(emailSessionId);

    // Look up signal_session_id (sig_xxx) from the email session table
    std::string sigSid = s_sessionRepo.getSignalSessionId(emailSid);
    if (sigSid.empty()) {
        LOG_INFO("[Signal] signal_session_exists_for_email_session: no signal_session_id for %s\n", emailSid.c_str());
        return 0;
    }

    // Check if this side can send: just verify we have the peer's prekey (public key)
    // No need to check send_n/recv_n - having the peer's prekey is sufficient
    std::string peerEmail = s_signalSessionRepo.getPeerEmail(acc, sigSid);
    SignalPeerPrekeyRecord pkRec;
    if (peerEmail.empty() || !s_signalKeyRepo.loadPeerPrekey(acc, peerEmail, pkRec)) {
        LOG_INFO("[Signal] signal_session_exists_for_email_session: sigSid=%s cannot send (no peer prekey) for %s\n",
                 sigSid.c_str(), emailSid.c_str());
        return 0;
    }

    LOG_INFO("[Signal] signal_session_exists_for_email_session: session ready, sigSid=%s for %s\n",
             sigSid.c_str(), emailSid.c_str());
    return 1;
}

extern "C" int signal_store_peer_prekey(const char* account, const char* peerEmail,
                                        const char* ikPub, const char* spkPub,
                                        const char* spkSig, const char* opkPub,
                                        const char* keyScope) {
    if (!account || !peerEmail || !ikPub || !spkPub) return -1;
    return s_signalKeyRepo.upsertPeerPrekey(account, peerEmail, ikPub, spkPub,
                                            spkSig ? spkSig : "",
                                            opkPub ? opkPub : "",
                                            keyScope ? keyScope : "") ? 0 : -1;
}
