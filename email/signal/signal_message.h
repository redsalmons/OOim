#ifndef SIGNAL_MESSAGE_H
#define SIGNAL_MESSAGE_H

#include "signal_protocol.h"
#include <string>
#include <nlohmann/json.hpp>

// Encode/decode Signal protocol messages into JSON for email body transport.
// The JSON structure is:
// {
//   "version": 1,
//   "session_id": "...",
//   "from": "alice@example.com",
//   "to": ["bob@example.com"],
//   "signal_header": {
//     "dh_pub": "...",
//     "n": 5,
//     "pn": 2,
//     "msg_type": "init|msg|repair"
//   },
//   "body": { ... }  // type-specific content
// }

namespace signal_msg {

// Build a SESSION_INIT (1.0.1) message JSON
// Includes X3DH info (ek_pub, ik_pub) + first encrypted payload
nlohmann::json encode_session_init(
    const std::string& sessionId,
    const std::string& fromAccount,
    const std::vector<std::string>& toRecipients,
    const EncryptedMessage& encMsg,
    const std::string& ikPub,        // sender's identity key (PEM)
    const std::string& ekPub,        // sender's ephemeral key (PEM)
    const PrekeyBundle& senderBundle, // sender's prekey bundle (for in-band exchange)
    const std::string& messageId = "",  // current email's locally generated message_id
    const std::string& inReplyTo = "",  // message_id of the email being replied to
    const std::string& peerSpkPub = "",  // peer's SPK pub that initiator used (so responder can look up matching priv)
    const std::string& peerIkPub = ""   // peer's IK pub that initiator used (so responder can look up matching priv)
);

// Build a RATCHET_MSG (1.0.2) message JSON
nlohmann::json encode_ratchet_msg(
    const std::string& sessionId,
    const std::string& fromAccount,
    const std::vector<std::string>& toRecipients,
    const EncryptedMessage& encMsg,
    const std::string& messageId = "",  // current email's locally generated message_id
    const std::string& inReplyTo = ""   // message_id of the email being replied to
);

// Build a PREKEY_BUNDLE (1.0.0) message JSON
// Used to distribute one's prekey bundle in-band
nlohmann::json encode_prekey_bundle(
    const std::string& fromAccount,
    const std::vector<std::string>& toRecipients,
    const PrekeyBundle& bundle
);

// Build a REPAIR_MSG (1.0.3) message JSON
nlohmann::json encode_repair_msg(
    const std::string& sessionId,
    const std::string& fromAccount,
    const std::vector<std::string>& toRecipients,
    const std::string& reason
);

// Parse a received message JSON into structured fields
struct ParsedSignalMessage {
    int version = 1;
    std::string sessionId;
    std::string fromAccount;
    std::vector<std::string> toRecipients;
    SignalHeader header;
    std::string ciphertext;       // base64
    std::string ekPub;            // only for init
    std::string ikPub;            // only for init
    std::string peerSpkPub;       // only for init: peer's SPK pub that initiator used
    std::string peerIkPub;        // only for init: peer's IK pub that initiator used
    PrekeyBundle senderBundle;    // only for prekey_bundle / init
    std::string repairReason;     // only for repair
    std::string messageId;         // current email's locally generated message_id
    std::string inReplyTo;         // message_id of the email being replied to
    bool hasPrekeyBundle = false;
    bool isInit = false;
    bool isRepair = false;
};

bool decode_signal_message(const nlohmann::json& j, ParsedSignalMessage& out);

// Convenience: decode from string
bool decode_signal_message_str(const std::string& jsonStr, ParsedSignalMessage& out);

// Convenience: encode to string
std::string encode_to_str(const nlohmann::json& j);

} // namespace signal_msg

#endif // SIGNAL_MESSAGE_H
