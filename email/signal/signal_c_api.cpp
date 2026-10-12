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

static int write_out(char* outJson, int outSize, const json& j) {
    std::string s = j.dump();
    snprintf(outJson, outSize, "%s", s.c_str());
    return 0;
}

static int write_error(char* outJson, int outSize, const char* error, int rc) {
    write_out(outJson, outSize, json{{"status", "error"}, {"error", error}});
    return rc;
}

// TOFU: first bundle from a peer pins its IK; a different IK afterwards is rejected.
// Returns 0 ok, -2 bad signature, -3 identity changed.
static int verify_and_pin_peer(const std::string& account, const std::string& peer,
                               const PrekeyBundle& bundle) {
    if (!verify_prekey_bundle(bundle)) {
        LOG_INFO("[Signal] peer %s: invalid SPK signature, bundle rejected\n", peer.c_str());
        return -2;
    }
    return pin_peer_identity(account, peer, bundle.ikPub);
}

static int bundle_for_session(const std::string& acc, const std::string& sigId,
                              char* outJson, int outSize) {
    if (!ensure_identity_key(acc)) return write_error(outJson, outSize, "no_identity_key", -2);
    if (!ensure_signed_prekey_for_session(acc, sigId)) return write_error(outJson, outSize, "no_signed_prekey", -3);

    PrekeyBundle bundle = build_prekey_bundle_for_session(acc, sigId);
    if (bundle.ikPub.empty()) return write_error(outJson, outSize, "no_identity_key", -2);

    return write_out(outJson, outSize, json{
        {"status", "success"},
        {"session_id", sigId},
        {"prekey_bundle", {{"ik_pub", bundle.ikPub}, {"spk_pub", bundle.spkPub}, {"spk_sig", bundle.spkSig}}}
    });
}

extern "C" int signal_get_prekey_bundle(const char* account, char* outJson, int outSize) {
    if (!account || !outJson || outSize <= 0) return -1;
    std::string sigId = generate_session_id();
    int rc = bundle_for_session(account, sigId, outJson, outSize);
    if (rc == 0) LOG_INFO("[Signal] signal_get_prekey_bundle: session_id=%s for %s\n", sigId.c_str(), account);
    return rc;
}

extern "C" int signal_get_prekey_bundle_for_session(const char* account, const char* sessionId,
                                                    char* outJson, int outSize) {
    if (!account || !sessionId || !*sessionId || !outJson || outSize <= 0) return -1;
    return bundle_for_session(account, sessionId, outJson, outSize);
}

extern "C" int signal_store_peer_prekey(const char* account, const char* peerEmail,
                                        const char* sessionId, const char* ikPub,
                                        const char* spkPub, const char* spkSig) {
    if (!account || !peerEmail || !sessionId || !*sessionId || !ikPub || !spkPub || !spkSig) return -1;
    PrekeyBundle bundle{ikPub, spkPub, spkSig};
    int rc = verify_and_pin_peer(account, peerEmail, bundle);
    if (rc != 0) return rc;
    return s_signalKeyRepo.upsertPeerPrekey(account, peerEmail, sessionId, ikPub, spkPub, spkSig) ? 0 : -1;
}

extern "C" int signal_session_initiate_with_id(const char* account, const char* peerEmail,
                                               const char* sessionId,
                                               const char* plaintext, char* outJson, int outSize,
                                               const char* messageId,
                                               const char* inReplyTo) {
    if (!account || !peerEmail || !sessionId || !plaintext || !outJson || outSize <= 0) return -1;

    std::string acc(account), peer(peerEmail), sid(sessionId), text(plaintext);
    if (sid.empty()) return -1;

    SignalPeerPrekeyRecord peerRec;
    if (!s_signalKeyRepo.loadPeerPrekey(acc, peer, sid, peerRec)) {
        LOG_INFO("[Signal] initiate: no peer prekey for %s session=%s\n", peer.c_str(), sid.c_str());
        return write_error(outJson, outSize, "no_peer_prekey", -4);
    }
    PrekeyBundle peerBundle{peerRec.ikPub, peerRec.spkPub, peerRec.spkSig};

    // Our SPK for this session was created when our own bundle went out.
    PrekeyBundle myBundle = build_prekey_bundle_for_session(acc, sid);
    if (myBundle.ikPub.empty()) return write_error(outJson, outSize, "no_signed_prekey", -3);

    std::string rootKey, ekPub, ekPriv, ekPassword;
    if (!x3dh_initiate(acc, peerBundle, rootKey, ekPub, ekPriv, ekPassword)) {
        return write_error(outJson, outSize, "x3dh_failed", -6);
    }
    if (!dr_initialize(acc, peer, sid, rootKey, peerBundle.spkPub, true)) {
        return write_error(outJson, outSize, "dr_init_failed", -7);
    }

    signal_msg::Envelope env;
    env.type = "session_init";
    env.sessionId = sid;
    env.fromAccount = acc;
    env.toRecipients = {peer};
    env.messageId = messageId ? messageId : "";
    env.inReplyTo = inReplyTo ? inReplyTo : "";
    env.ekPub = ekPub;
    env.ikPub = myBundle.ikPub;
    env.peerSpkPub = peerBundle.spkPub;
    env.senderBundle = myBundle;

    EncryptedMessage encMsg;
    encMsg.header.msgType = "init";
    json envelope;
    auto adBuilder = [&](const SignalHeader& h) {
        envelope = signal_msg::encode_envelope(env, h);
        return signal_msg::associated_data(envelope);
    };
    if (!dr_encrypt(acc, peer, sid, text, adBuilder, encMsg)) {
        return write_error(outJson, outSize, "dr_encrypt_failed", -8);
    }
    envelope["body"]["ciphertext"] = encMsg.ciphertext;

    LOG_INFO("[Signal] initiate: session %s for %s -> %s\n", sid.c_str(), acc.c_str(), peer.c_str());
    return write_out(outJson, outSize, json{{"status", "success"}, {"session_id", sid}, {"message", envelope}});
}

extern "C" int signal_session_encrypt(const char* account, const char* peerEmail,
                                      const char* sessionId, const char* plaintext,
                                      char* outJson, int outSize,
                                      const char* messageId,
                                      const char* inReplyTo) {
    if (!account || !peerEmail || !sessionId || !plaintext || !outJson || outSize <= 0) return -1;

    std::string acc(account), peer(peerEmail), sid(sessionId), text(plaintext);
    if (sid.empty()) return write_error(outJson, outSize, "no_session_id_provided", -2);

    signal_msg::Envelope env;
    env.type = "ratchet_msg";
    env.sessionId = sid;
    env.fromAccount = acc;
    env.toRecipients = {peer};
    env.messageId = messageId ? messageId : "";
    env.inReplyTo = inReplyTo ? inReplyTo : "";

    EncryptedMessage encMsg;
    json envelope;
    auto adBuilder = [&](const SignalHeader& h) {
        envelope = signal_msg::encode_envelope(env, h);
        return signal_msg::associated_data(envelope);
    };
    if (!dr_encrypt(acc, peer, sid, text, adBuilder, encMsg)) {
        return write_error(outJson, outSize, "encrypt_failed", -3);
    }
    envelope["body"]["ciphertext"] = encMsg.ciphertext;

    return write_out(outJson, outSize, json{{"status", "success"}, {"session_id", sid}, {"message", envelope}});
}

extern "C" int signal_session_decrypt(const char* account, const char* peerEmail,
                                      const char* jsonBody, char* outJson, int outSize) {
    if (!account || !peerEmail || !jsonBody || !outJson || outSize <= 0) return -1;

    std::string acc(account);

    signal_msg::ParsedSignalMessage parsed;
    if (!signal_msg::decode_signal_message_str(jsonBody, parsed)) {
        return write_error(outJson, outSize, "parse_failed", -2);
    }
    if (parsed.version != 2 || (parsed.type != "session_init" && parsed.type != "ratchet_msg")) {
        return write_error(outJson, outSize, "unsupported_version_or_type", -2);
    }

    // The authenticated body `from` is the identity; the envelope sender is only the courier.
    std::string peer = parsed.fromAccount;
    if (peer.empty() || parsed.sessionId.empty()) {
        return write_error(outJson, outSize, "missing_from_or_session", -2);
    }
    const std::string& sigId = parsed.sessionId;

    auto fail = [&](const char* err, int rc) {
        return write_out(outJson, outSize, json{
            {"status", "error"}, {"error", err}, {"session_id", sigId},
            {"x_message_id", parsed.messageId}, {"x_reply_to", parsed.inReplyTo}}), rc;
    };

    if (parsed.isInit) {
        if (!parsed.hasPrekeyBundle || parsed.senderBundle.ikPub != parsed.ikPub) {
            return fail("init_missing_bundle", -3);
        }
        // Initiator's bundle: signature + pinned identity.
        int pin = verify_and_pin_peer(acc, peer, parsed.senderBundle);
        if (pin == -2) return fail("bad_spk_signature", -3);
        if (pin == -3) return fail("identity_changed", -9);

        // The initiator must have used the SPK we published for this session.
        SignalPrekeyRecord spk;
        if (parsed.peerSpkPub.empty() ||
            !s_signalKeyRepo.loadSignedPrekeyByPub(acc, parsed.peerSpkPub, spk) ||
            spk.sessionUuid != sigId) {
            LOG_INFO("[Signal] decrypt: init for session %s references an unknown SPK\n", sigId.c_str());
            return fail("unknown_spk", -4);
        }

        // Duplicate / replayed init must not reset an established ratchet.
        SignalSessionRecord existing;
        if (!s_signalSessionRepo.loadSession(acc, peer, sigId, existing)) {
            s_signalKeyRepo.upsertPeerPrekey(acc, peer, sigId, parsed.senderBundle.ikPub,
                                             parsed.senderBundle.spkPub, parsed.senderBundle.spkSig);
            std::string rootKey;
            if (!x3dh_respond(acc, sigId, parsed.ikPub, parsed.ekPub, rootKey)) {
                return fail("x3dh_respond_failed", -5);
            }
            if (!dr_initialize(acc, peer, sigId, rootKey, parsed.header.dhPub, false)) {
                return fail("dr_init_failed", -6);
            }
        }
    } else {
        // Ratchet messages are only accepted from the pinned identity of a known session.
        SignalSessionRecord existing;
        if (!s_signalSessionRepo.loadSession(acc, peer, sigId, existing)) {
            return fail("session_not_found", -5);
        }
    }

    EncryptedMessage encMsg;
    encMsg.header = parsed.header;
    encMsg.ciphertext = parsed.ciphertext;
    encMsg.ekPub = parsed.ekPub;
    encMsg.ikPub = parsed.ikPub;

    DecryptResult result = dr_decrypt(acc, peer, sigId, encMsg, parsed.ad);
    LOG_INFO("[Signal] decrypt: from=%s session=%s success=%d%s%s\n", peer.c_str(), sigId.c_str(),
             result.success ? 1 : 0, result.success ? "" : " error=", result.success ? "" : result.error.c_str());
    if (!result.success) return fail(result.error.c_str(), -5);

    return write_out(outJson, outSize, json{
        {"status", "success"}, {"plaintext", result.plaintext}, {"session_id", sigId},
        {"x_message_id", parsed.messageId}, {"x_reply_to", parsed.inReplyTo}});
}

extern "C" int signal_session_exists(const char* account, const char* peerEmail,
                                     const char* sessionId) {
    if (!account || !peerEmail || !sessionId || !*sessionId) return -1;
    SignalSessionRecord rec;
    return s_signalSessionRepo.loadSession(account, peerEmail, sessionId, rec) ? 1 : 0;
}

extern "C" int signal_session_close(const char* account, const char* peerEmail,
                                    const char* sessionId) {
    if (!account || !peerEmail || !sessionId) return -1;
    return s_signalSessionRepo.closeSession(account, peerEmail, sessionId) ? 0 : -1;
}

extern "C" int signal_session_exists_for_email_session(const char* account,
                                                         const char* emailSessionId) {
    if (!account || !emailSessionId) return -1;
    std::string acc(account), emailSid(emailSessionId);

    std::string sigSid = s_sessionRepo.getSignalSessionId(emailSid);
    if (sigSid.empty()) return 0;

    std::string peerEmail = s_signalSessionRepo.getPeerEmail(acc, sigSid);
    SignalPeerPrekeyRecord pkRec;
    if (peerEmail.empty() || !s_signalKeyRepo.loadPeerPrekey(acc, peerEmail, sigSid, pkRec)) return 0;
    return 1;
}

extern "C" int signal_identity_fingerprint(const char* account, char* out, int outSize) {
    if (!account || !out || outSize <= 0) return -1;
    snprintf(out, outSize, "%s", key_fingerprint(identity_public_key(account)).c_str());
    return 0;
}

extern "C" int signal_peer_identity_fingerprint(const char* account, const char* peerEmail,
                                                char* out, int outSize) {
    if (!account || !peerEmail || !out || outSize <= 0) return -1;
    SignalPeerIdentityRecord rec;
    std::string fp = s_signalKeyRepo.loadPeerIdentity(account, peerEmail, rec) ? key_fingerprint(rec.ikPub) : "";
    snprintf(out, outSize, "%s", fp.c_str());
    return 0;
}

extern "C" int signal_peer_identity_reset(const char* account, const char* peerEmail) {
    if (!account || !peerEmail) return -1;
    return s_signalKeyRepo.deletePeerIdentity(account, peerEmail) ? 0 : -1;
}
