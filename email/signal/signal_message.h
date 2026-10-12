#ifndef SIGNAL_MESSAGE_H
#define SIGNAL_MESSAGE_H

#include "signal_protocol.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// Signal wire format v2 (email body JSON):
// {
//   "version": 2,
//   "type": "prekey_bundle" | "session_init" | "ratchet_msg",
//   "session_id": "sig_...",
//   "from": "alice@example.com",
//   "to": ["bob@example.com"],
//   "x_message_id": "<...>",          // body-level id of this message
//   "x_reply_to": "<...>",            // body-level id of the previous message
//   "signal_header": {"dh_pub", "n", "pn", "msg_type"},   // init / msg only
//   "body": {
//     "ciphertext": "...",            // init / msg
//     "ek_pub", "ik_pub",             // init: initiator's X3DH keys
//     "peer_spk_pub",                 // init: responder SPK the initiator used
//     "prekey_bundle": {"ik_pub","spk_pub","spk_sig"}   // bundle / init
//   }
// }
// Associated data for the AEAD is the canonical dump of the whole envelope with
// body.ciphertext removed, so every field above is authenticated.

namespace signal_msg {

struct Envelope {
    std::string type;
    std::string sessionId;
    std::string fromAccount;
    std::vector<std::string> toRecipients;
    std::string messageId;
    std::string inReplyTo;
    // init only
    std::string ekPub;
    std::string ikPub;
    std::string peerSpkPub;
    PrekeyBundle senderBundle;
};

// Envelope without ciphertext; header is filled in by the ratchet.
nlohmann::json encode_envelope(const Envelope& env, const SignalHeader& header);

// Standalone PREKEY_BUNDLE body (no ratchet header).
nlohmann::json encode_prekey_bundle(const std::string& sessionId,
                                    const std::string& messageId,
                                    const std::string& inReplyTo,
                                    const PrekeyBundle& bundle);

// Canonical AD of an envelope: json with body.ciphertext removed, dumped.
std::string associated_data(nlohmann::json envelope);

struct ParsedSignalMessage {
    int version = 0;
    std::string type;
    std::string sessionId;
    std::string fromAccount;
    std::vector<std::string> toRecipients;
    std::string messageId;
    std::string inReplyTo;
    SignalHeader header;
    std::string ciphertext;
    std::string ekPub;
    std::string ikPub;
    std::string peerSpkPub;
    PrekeyBundle senderBundle;
    bool hasPrekeyBundle = false;
    bool isInit = false;
    std::string ad;   // associated data recomputed from the received envelope
};

bool decode_signal_message_str(const std::string& jsonStr, ParsedSignalMessage& out);

} // namespace signal_msg

#endif // SIGNAL_MESSAGE_H
