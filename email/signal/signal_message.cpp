#include "signal_message.h"
#include "logger.h"

namespace signal_msg {

static nlohmann::json bundle_json(const PrekeyBundle& b) {
    return {{"ik_pub", b.ikPub}, {"spk_pub", b.spkPub}, {"spk_sig", b.spkSig}};
}

nlohmann::json encode_envelope(const Envelope& env, const SignalHeader& header) {
    nlohmann::json j;
    j["version"] = 2;
    j["type"] = env.type;
    j["session_id"] = env.sessionId;
    j["from"] = env.fromAccount;
    j["to"] = env.toRecipients;
    j["x_message_id"] = env.messageId;
    j["x_reply_to"] = env.inReplyTo;
    j["signal_header"] = {
        {"dh_pub", header.dhPub},
        {"n", header.n},
        {"pn", header.pn},
        {"msg_type", header.msgType}
    };
    j["body"] = nlohmann::json::object();
    if (env.type == "session_init") {
        j["body"]["ek_pub"] = env.ekPub;
        j["body"]["ik_pub"] = env.ikPub;
        j["body"]["peer_spk_pub"] = env.peerSpkPub;
        j["body"]["prekey_bundle"] = bundle_json(env.senderBundle);
    }
    return j;
}

nlohmann::json encode_prekey_bundle(const std::string& sessionId,
                                    const std::string& messageId,
                                    const std::string& inReplyTo,
                                    const PrekeyBundle& bundle) {
    return {
        {"version", 2},
        {"type", "prekey_bundle"},
        {"session_id", sessionId},
        {"x_message_id", messageId},
        {"x_reply_to", inReplyTo},
        {"prekey_bundle", bundle_json(bundle)}
    };
}

std::string associated_data(nlohmann::json envelope) {
    if (envelope.contains("body") && envelope["body"].is_object()) {
        envelope["body"].erase("ciphertext");
    }
    return envelope.dump();
}

bool decode_signal_message_str(const std::string& jsonStr, ParsedSignalMessage& out) {
    try {
        auto j = nlohmann::json::parse(jsonStr);
        out.version = j.value("version", 0);
        out.type = j.value("type", "");
        out.sessionId = j.value("session_id", "");
        out.fromAccount = j.value("from", "");
        out.messageId = j.value("x_message_id", "");
        out.inReplyTo = j.value("x_reply_to", "");
        if (j.contains("to") && j["to"].is_array()) {
            for (const auto& r : j["to"]) out.toRecipients.push_back(r.get<std::string>());
        }
        if (j.contains("signal_header")) {
            auto& h = j["signal_header"];
            out.header.dhPub = h.value("dh_pub", "");
            out.header.n = h.value("n", 0);
            out.header.pn = h.value("pn", 0);
            out.header.msgType = h.value("msg_type", "");
        }
        const nlohmann::json* bundle = nullptr;
        if (j.contains("body") && j["body"].is_object()) {
            auto& body = j["body"];
            out.ciphertext = body.value("ciphertext", "");
            out.ekPub = body.value("ek_pub", "");
            out.ikPub = body.value("ik_pub", "");
            out.peerSpkPub = body.value("peer_spk_pub", "");
            if (body.contains("prekey_bundle")) bundle = &body["prekey_bundle"];
        }
        if (!bundle && j.contains("prekey_bundle")) bundle = &j["prekey_bundle"];
        if (bundle) {
            out.senderBundle.ikPub = bundle->value("ik_pub", "");
            out.senderBundle.spkPub = bundle->value("spk_pub", "");
            out.senderBundle.spkSig = bundle->value("spk_sig", "");
            out.hasPrekeyBundle = !out.senderBundle.ikPub.empty();
        }
        out.isInit = (out.type == "session_init");
        out.ad = associated_data(j);
        return true;
    } catch (const std::exception& e) {
        LOG_INFO("[Signal] decode_signal_message_str: parse error: %s\n", e.what());
        return false;
    }
}

} // namespace signal_msg
