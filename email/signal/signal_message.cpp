#include "signal_message.h"
#include "logger.h"

namespace signal_msg {

nlohmann::json encode_session_init(
    const std::string& sessionId,
    const std::string& fromAccount,
    const std::vector<std::string>& toRecipients,
    const EncryptedMessage& encMsg,
    const std::string& ikPub,
    const std::string& ekPub,
    const PrekeyBundle& senderBundle,
    const std::string& messageId,
    const std::string& inReplyTo,
    const std::string& peerSpkPub,
    const std::string& peerIkPub) {

    nlohmann::json j;
    j["version"] = 1;
    j["session_id"] = sessionId;
    j["from"] = fromAccount;
    j["to"] = toRecipients;
    if (!messageId.empty()) {
        j["x_message_id"] = messageId;
    }
    if (!inReplyTo.empty()) {
        j["x_reply_to"] = inReplyTo;
    }

    j["signal_header"] = {
        {"dh_pub", encMsg.header.dhPub},
        {"n", encMsg.header.n},
        {"pn", encMsg.header.pn},
        {"msg_type", "init"}
    };

    j["body"] = {
        {"ciphertext", encMsg.ciphertext},
        {"ek_pub", ekPub},
        {"ik_pub", ikPub},
        {"prekey_bundle", {
            {"ik_pub", senderBundle.ikPub},
            {"spk_pub", senderBundle.spkPub},
            {"spk_sig", senderBundle.spkSig},
            {"opk_pub", senderBundle.opkPub}
        }}
    };
    if (!peerSpkPub.empty()) {
        j["body"]["peer_spk_pub"] = peerSpkPub;
    }
    if (!peerIkPub.empty()) {
        j["body"]["peer_ik_pub"] = peerIkPub;
    }

    return j;
}

nlohmann::json encode_ratchet_msg(
    const std::string& sessionId,
    const std::string& fromAccount,
    const std::vector<std::string>& toRecipients,
    const EncryptedMessage& encMsg,
    const std::string& messageId,
    const std::string& inReplyTo) {

    nlohmann::json j;
    j["version"] = 1;
    j["session_id"] = sessionId;
    j["from"] = fromAccount;
    j["to"] = toRecipients;
    if (!messageId.empty()) {
        j["x_message_id"] = messageId;
    }
    if (!inReplyTo.empty()) {
        j["x_reply_to"] = inReplyTo;
    }

    j["signal_header"] = {
        {"dh_pub", encMsg.header.dhPub},
        {"n", encMsg.header.n},
        {"pn", encMsg.header.pn},
        {"msg_type", "msg"}
    };

    j["body"] = {
        {"ciphertext", encMsg.ciphertext}
    };

    return j;
}

nlohmann::json encode_prekey_bundle(
    const std::string& fromAccount,
    const std::vector<std::string>& toRecipients,
    const PrekeyBundle& bundle) {

    nlohmann::json j;
    j["version"] = 1;
    j["session_id"] = "";
    j["from"] = fromAccount;
    j["to"] = toRecipients;

    j["signal_header"] = {
        {"dh_pub", ""},
        {"n", 0},
        {"pn", 0},
        {"msg_type", "prekey"}
    };

    j["body"] = {
        {"prekey_bundle", {
            {"ik_pub", bundle.ikPub},
            {"spk_pub", bundle.spkPub},
            {"spk_sig", bundle.spkSig},
            {"opk_pub", bundle.opkPub}
        }}
    };

    return j;
}

nlohmann::json encode_repair_msg(
    const std::string& sessionId,
    const std::string& fromAccount,
    const std::vector<std::string>& toRecipients,
    const std::string& reason) {

    nlohmann::json j;
    j["version"] = 1;
    j["session_id"] = sessionId;
    j["from"] = fromAccount;
    j["to"] = toRecipients;

    j["signal_header"] = {
        {"dh_pub", ""},
        {"n", 0},
        {"pn", 0},
        {"msg_type", "repair"}
    };

    j["body"] = {
        {"reason", reason}
    };

    return j;
}

bool decode_signal_message(const nlohmann::json& j, ParsedSignalMessage& out) {
    try {
        out.version = j.value("version", 1);
        out.sessionId = j.value("session_id", "");
        out.fromAccount = j.value("from", "");
        out.messageId = j.value("x_message_id", "");
        out.inReplyTo = j.value("x_reply_to", "");

        if (j.contains("to") && j["to"].is_array()) {
            for (const auto& r : j["to"]) {
                out.toRecipients.push_back(r.get<std::string>());
            }
        }

        if (j.contains("signal_header")) {
            auto& h = j["signal_header"];
            out.header.dhPub = h.value("dh_pub", "");
            out.header.n = h.value("n", 0);
            out.header.pn = h.value("pn", 0);
            out.header.msgType = h.value("msg_type", "");
        }

        if (j.contains("body")) {
            auto& body = j["body"];
            out.ciphertext = body.value("ciphertext", "");
            out.ekPub = body.value("ek_pub", "");
            out.ikPub = body.value("ik_pub", "");
            out.peerSpkPub = body.value("peer_spk_pub", "");
            out.peerIkPub = body.value("peer_ik_pub", "");

            if (body.contains("prekey_bundle")) {
                auto& pb = body["prekey_bundle"];
                out.senderBundle.ikPub = pb.value("ik_pub", "");
                out.senderBundle.spkPub = pb.value("spk_pub", "");
                out.senderBundle.spkSig = pb.value("spk_sig", "");
                out.senderBundle.opkPub = pb.value("opk_pub", "");
                out.hasPrekeyBundle = !out.senderBundle.ikPub.empty();
            }

            out.repairReason = body.value("reason", "");
        }

        out.isInit = (out.header.msgType == "init");
        out.isRepair = (out.header.msgType == "repair");

        return true;
    } catch (const std::exception& e) {
        LOG_INFO("[Signal] decode_signal_message: parse error: %s\n", e.what());
        return false;
    }
}

bool decode_signal_message_str(const std::string& jsonStr, ParsedSignalMessage& out) {
    try {
        auto j = nlohmann::json::parse(jsonStr);
        return decode_signal_message(j, out);
    } catch (const std::exception& e) {
        LOG_INFO("[Signal] decode_signal_message_str: parse error: %s\n", e.what());
        return false;
    }
}

std::string encode_to_str(const nlohmann::json& j) {
    return j.dump();
}

} // namespace signal_msg
