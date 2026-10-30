// Two-party Signal (X3DH + Double Ratchet, wire v2) round trip through the C API.
// Alice and Bob share one in-memory database; their state is separated by account.
#include "email_core.h"
#include "email_core_common.h"
#include "signal_protocol.h"
#include "signal_message.h"
#include "libemail/x_mailer.h"
#include "email_body_repo.h"
#include "email_repo.h"
#include "file_chunk_util.h"
#include <vmime/vmime.hpp>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <random>

using json = nlohmann::json;

static int g_failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); g_failures++; } } while (0)
#define REQUIRE(cond, msg) do { if (!(cond)) { fprintf(stderr, "FATAL %s:%d: %s\n", __FILE__, __LINE__, msg); exit(1); } } while (0)

static const char* A = "alice@example.com";
static const char* B = "bob@example.com";

static std::string mime_roundtrip(const std::string& payload, const std::string& type) {
    vmime::messageBuilder builder;
    builder.setExpeditor(vmime::mailbox(A));
    vmime::addressList recipients;
    recipients.appendAddress(vmime::make_shared<vmime::mailbox>(B));
    builder.setRecipients(recipients);
    builder.setSubject(vmime::text("OceanTalk transport test"));
    builder.getTextPart()->setCharset(vmime::charset("UTF-8"));
    builder.getTextPart()->setText(vmime::make_shared<vmime::stringContentHandler>(
        oim_encode_wire_body(payload, type)));
    auto msg = builder.construct();
    msg->getHeader()->appendField(
        vmime::headerFieldFactory::getInstance()->create("X-Mailer", XMailer::WIRE_VALUE));
    // Deliberately unrelated header IDs: transport must preserve body IDs.
    msg->getHeader()->MessageId()->setValue("<unrelated-header@example.com>");
    const auto eml = msg->generate();
    CHECK(eml.find("message.oim") == std::string::npos, "no protocol attachment");
    CHECK(eml.find("application/octet-stream") == std::string::npos, "no binary MIME part");
    std::string decoded, decodedType;
    REQUIRE(oim_parse_protocol_mail(eml, decoded, decodedType), "parse MIME armor");
    CHECK(decodedType == type, "internal protocol type preserved");
    CHECK(decoded == payload, "payload and body IDs byte-for-byte preserved");
    return decoded;
}

static void test_wire_format() {
    const std::string payload = R"({"x-message-id":"<body-id>","x-reply-to":"<parent>","text":"中文\npayload"})";
    for (const auto* type : {XMailer::PREKEY_BUNDLE, XMailer::SESSION_INIT,
                            XMailer::RATCHET_MSG, XMailer::REPAIR_MSG,
                            XMailer::ATTACH_META, XMailer::ATTACH_CHUNK,
                            XMailer::MLS_KEY_PACKAGE, XMailer::MLS_WELCOME,
                            XMailer::MLS_COMMIT, XMailer::MLS_APP_MSG,
                            XMailer::MLS_FILE_META, XMailer::MLS_FILE_CHUNK}) {
        mime_roundtrip(payload, type);
    }
    // Graph's Text body and standard MIME transfer encodings must decode alike.
    auto wire = oim_encode_wire_body(payload, XMailer::PREKEY_BUNDLE);
    std::string crlf;
    for (char c : wire) crlf += c == '\n' ? "\r\n" : std::string(1, c);
    const std::string headers =
        "X-Mailer: OceanTalk\r\nMIME-Version: 1.0\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n";
    for (const auto* encoding : {"8bit", "base64", "quoted-printable"}) {
        std::string body = crlf;
        if (std::string(encoding) == "base64") {
            body = base64_encode(reinterpret_cast<const unsigned char*>(crlf.data()), crlf.size());
        } else if (std::string(encoding) == "quoted-printable") {
            body.clear();
            for (unsigned char c : crlf) {
                char hex[4];
                snprintf(hex, sizeof(hex), "=%02X", c);
                body += hex;
                body += "=\r\n"; // exercise soft line wrapping
            }
        }
        std::string decoded, type;
        REQUIRE(oim_parse_protocol_mail(headers + "Content-Transfer-Encoding: " +
                encoding + "\r\n\r\n" + body, decoded, type), "MIME transfer encoding");
        CHECK(decoded == payload && type == XMailer::PREKEY_BUNDLE, "decoded Graph/MIME body");
    }
    std::string decoded, type;
    CHECK(!oim_parse_protocol_mail(headers + "\r\nordinary text", decoded, type),
          "marker without armor is not protocol data");
    CHECK(!oim_decode_wire_body("ordinary email", decoded, type), "ordinary text unchanged");
    for (const auto& broken : {
            wire.substr(0, wire.find("-----END")),
            std::string("-----BEGIN OCEANTALK MESSAGE-----\n!!!!\n-----END OCEANTALK MESSAGE-----"),
            wire + wire}) {
        bool rejected = false;
        try { oim_decode_wire_body(broken, decoded, type); }
        catch (const std::exception&) { rejected = true; }
        CHECK(rejected, "malformed/truncated/duplicate armor rejected");
    }
    for (const auto& envelope : {
            json{{"wire_version", 99}, {"message_type", "1.0.0"}, {"payload", payload}},
            json{{"wire_version", 1}, {"message_type", "invalid"}, {"payload", payload}}}) {
        const auto zip = oim_zip_pack(envelope.dump());
        const auto armor = std::string("-----BEGIN OCEANTALK MESSAGE-----\n") +
            base64_encode(zip.data(), zip.size()) + "\n-----END OCEANTALK MESSAGE-----\n";
        bool rejected = false;
        try { oim_decode_wire_body(armor, decoded, type); }
        catch (const std::exception&) { rejected = true; }
        CHECK(rejected, "unknown envelope version/type rejected");
    }
}

static void test_stored_wire_body() {
    REQUIRE(sqlite3_exec(g_db,
        "CREATE TABLE email_body(email_id INTEGER PRIMARY KEY, body TEXT);"
        "CREATE TABLE localemail(uuid TEXT, account TEXT, x_mailer TEXT, visible INTEGER);"
        "INSERT INTO localemail VALUES('1','alice@example.com','OceanTalk',0);"
        "INSERT INTO localemail VALUES('1','bob@example.com','OceanTalk',0);",
        nullptr, nullptr, nullptr) == SQLITE_OK, "transport test tables");
    EmailBodyRepo bodies;
    EmailRepo emails;
    const std::string payload = R"({"x-message-id":"<body>","x-reply-to":"<parent>"})";
    const std::string prefix = "X-Mailer: OceanTalk\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n";
    REQUIRE(bodies.upsert(1, prefix + oim_encode_wire_body(payload, XMailer::MLS_WELCOME)),
            "persist received armor");
    char buffer[4096];
    REQUIRE(email_parse_body(1, buffer, sizeof(buffer)) == 0, "parse stored wire body");
    auto parsed = json::parse(buffer);
    CHECK(parsed["x_mailer"] == XMailer::MLS_WELCOME && parsed["text_body"] == payload,
          "UI receives internal type and original protocol body");
    CHECK(parsed["has_attachments"] == false, "armor is not a UI attachment");
    REQUIRE(emails.setProtocolType("1", A, XMailer::MLS_FILE_CHUNK, false), "persist chunk type");
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(g_db, "SELECT x_mailer, visible FROM localemail ORDER BY account",
                              -1, &stmt, nullptr) == SQLITE_OK, "read classification");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW, "alice row");
    CHECK(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))) ==
          XMailer::MLS_FILE_CHUNK && sqlite3_column_int(stmt, 1) == 0, "chunk remains hidden");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW, "bob row");
    CHECK(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))) ==
          "OceanTalk", "same UID in other account not modified");
    sqlite3_finalize(stmt);
    REQUIRE(bodies.upsert(1, "X-Mailer: 1.0.2\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nhello"),
            "persist decrypted local body");
    REQUIRE(email_parse_body(1, buffer, sizeof(buffer)) == 0, "read normalized local body");
    CHECK(json::parse(buffer)["text_body"] == "hello", "local plaintext does not require armor");
}

static json call_json(int (*fn)(const char*, const char*, const char*, const char*, char*, int, const char*, const char*),
                      const char* acc, const char* peer, const char* sid, const char* text,
                      const char* mid, const char* rt) {
    std::vector<char> buf(1 << 20);
    fn(acc, peer, sid, text, buf.data(), (int)buf.size(), mid, rt);
    return json::parse(buf.data());
}

static json encrypt(const char* acc, const char* peer, const std::string& sid, const std::string& text,
                    const std::string& mid) {
    return call_json(signal_session_encrypt, acc, peer, sid.c_str(), text.c_str(), mid.c_str(), "<prev>");
}

static json decrypt(const char* acc, const json& message) {
    std::vector<char> buf(1 << 20);
    std::string body = mime_roundtrip(message.dump(),
        message.value("type", "") == "session_init" ? XMailer::SESSION_INIT : XMailer::RATCHET_MSG);
    signal_session_decrypt(acc, "courier@relay", body.c_str(), buf.data(), (int)buf.size());
    return json::parse(buf.data());
}

static bool store_bundle(const char* acc, const char* peer, const json& bundleResp) {
    auto& pb = bundleResp["prekey_bundle"];
    std::string sid = bundleResp["session_id"];
    return signal_store_peer_prekey(acc, peer, sid.c_str(),
                                    pb["ik_pub"].get<std::string>().c_str(),
                                    pb["spk_pub"].get<std::string>().c_str(),
                                    pb["spk_sig"].get<std::string>().c_str()) == 0;
}

// Full handshake: A publishes a bundle, B answers for the same session, A sends init.
static std::string handshake(std::string& initCiphertext) {
    std::vector<char> buf(1 << 20);
    REQUIRE(signal_get_prekey_bundle(A, buf.data(), (int)buf.size()) == 0, "alice bundle");
    json aBundle = json::parse(buf.data());
    std::string sid = aBundle["session_id"];

    REQUIRE(store_bundle(B, A, aBundle), "bob stores alice bundle");
    REQUIRE(signal_get_prekey_bundle_for_session(B, sid.c_str(), buf.data(), (int)buf.size()) == 0, "bob bundle");
    json bBundle = json::parse(buf.data());
    REQUIRE(store_bundle(A, B, bBundle), "alice stores bob bundle");

    json init = call_json(signal_session_initiate_with_id, A, B, sid.c_str(), "hello bob", "<m1>", "<root>");
    REQUIRE(init["status"] == "success", "initiate");
    CHECK(init["message"]["version"] == 2, "wire version 2");
    CHECK(init["message"]["type"] == "session_init", "type session_init");
    initCiphertext = init["message"]["body"]["ciphertext"];

    json dec = decrypt(B, init["message"]);
    CHECK(dec["status"] == "success", "bob decrypts init");
    CHECK(dec["plaintext"] == "hello bob", "init plaintext");
    CHECK(dec["x_message_id"] == "<m1>", "body id passthrough");

    // Replayed init must neither reset the ratchet nor decrypt twice.
    json replay = decrypt(B, init["message"]);
    CHECK(replay["status"] == "error", "replayed init rejected");
    return sid;
}

static void test_bidirectional_and_out_of_order(const std::string& sid) {
    // Alice -> Bob on the first chain
    json a2 = encrypt(A, B, sid, "a2", "<a2>");
    json a3 = encrypt(A, B, sid, "a3", "<a3>");
    json a4 = encrypt(A, B, sid, "a4", "<a4>");
    CHECK(a2["message"]["signal_header"]["n"] == 1, "a2 n=1");

    // a4 arrives before a3: a3's key is kept as a skipped key
    json d4 = decrypt(B, a4["message"]);
    CHECK(d4["status"] == "success" && d4["plaintext"] == "a4", "out-of-order a4");
    json d2 = decrypt(B, a2["message"]);
    CHECK(d2["status"] == "success" && d2["plaintext"] == "a2", "skipped a2");

    // Bob replies -> DH ratchet step on his side
    json b1 = encrypt(B, A, sid, "b1", "<b1>");
    json b2 = encrypt(B, A, sid, "b2", "<b2>");
    CHECK(b1["message"]["signal_header"]["pn"] == 0, "bob first chain pn=0");
    json e1 = decrypt(A, b1["message"]);
    CHECK(e1["status"] == "success" && e1["plaintext"] == "b1", "alice decrypts b1");

    // Alice answers on a new chain; pn must equal her previous chain length (4)
    json a5 = encrypt(A, B, sid, "a5", "<a5>");
    CHECK(a5["message"]["signal_header"]["pn"] == 4, "alice pn = previous chain length");
    CHECK(a5["message"]["signal_header"]["n"] == 0, "alice new chain n=0");

    // Bob receives a5 before a3 (old chain). a3 must still decrypt afterwards.
    json d5 = decrypt(B, a5["message"]);
    CHECK(d5["status"] == "success" && d5["plaintext"] == "a5", "bob decrypts a5 (ratchet)");
    json d3 = decrypt(B, a3["message"]);
    CHECK(d3["status"] == "success" && d3["plaintext"] == "a3", "late a3 from previous chain");

    // b2 still decrypts for Alice after she ratcheted
    json e2 = decrypt(A, b2["message"]);
    CHECK(e2["status"] == "success" && e2["plaintext"] == "b2", "alice decrypts b2");

    // Bob's next message: pn = his previous chain length (2)
    json b3 = encrypt(B, A, sid, "b3", "<b3>");
    CHECK(b3["message"]["signal_header"]["pn"] == 2, "bob pn=2");
    json e3 = decrypt(A, b3["message"]);
    CHECK(e3["status"] == "success" && e3["plaintext"] == "b3", "alice decrypts b3");
}

static void test_inline_file_chunks(const std::string& sid) {
    // One full production-size chunk plus a short final chunk. Random bytes
    // exercise multi-MB armor rather than collapsing to a tiny compressed body.
    std::string file(filechunk::kDefaultChunkSize + 137, '\0');
    std::mt19937 random(42);
    for (auto& byte : file) byte = static_cast<char>(random() & 0xff);
    std::string received;
    std::string parent = "<file-meta>";
    int index = 0;
    for (size_t offset = 0; offset < file.size(); offset += filechunk::kDefaultChunkSize, ++index) {
        const auto chunk = file.substr(offset, filechunk::kDefaultChunkSize);
        const auto id = "<inline-chunk-" + std::to_string(index) + ">";
        const auto data = base64_encode(reinterpret_cast<const unsigned char*>(chunk.data()), chunk.size());
        const json truck = {
            {"msg_type", "truck"}, {"file_id", "inline-file"}, {"chunk_index", index},
            {"chunk_data", data}, {"chunk_md5", compute_md5(chunk)},
            {"x_message_id", id}, {"x_reply_to", parent}};
        const auto plain = truck.dump();
        std::vector<char> encrypted(plain.size() * 2 + 65536);
        REQUIRE(signal_session_encrypt(A, B, sid.c_str(), plain.c_str(),
            encrypted.data(), static_cast<int>(encrypted.size()), id.c_str(), parent.c_str()) == 0,
            "encrypt full-size file chunk with existing Signal protocol");
        const auto result = json::parse(encrypted.data());
        REQUIRE(result["status"] == "success", "file chunk encryption succeeds");
        const auto envelope = mime_roundtrip(result["message"].dump(), XMailer::ATTACH_CHUNK);
        std::vector<char> decrypted(envelope.size() + 65536);
        REQUIRE(signal_session_decrypt(B, A, envelope.c_str(), decrypted.data(),
            static_cast<int>(decrypted.size())) == 0, "decrypt inline file chunk");
        const auto decoded = json::parse(decrypted.data());
        REQUIRE(decoded["status"] == "success", "inline chunk decryption succeeds");
        CHECK(decoded["plaintext"] == plain, "file protocol fields preserved exactly");
        const auto recovered = json::parse(decoded["plaintext"].get<std::string>());
        const auto bytes = base64_decode(recovered["chunk_data"].get<std::string>());
        const std::string restored(bytes.begin(), bytes.end());
        CHECK(compute_md5(restored) == recovered["chunk_md5"], "chunk checksum unchanged");
        received += restored;

        // Transport-only MLS fixture: opaque ciphertext is never interpreted
        // or transformed into Signal data by the inline wrapper.
        const json mlsWire = {
            {"type", "mls_file_chunk"}, {"x_session_id", "group-fixture"},
            {"x_message_id", id}, {"x_reply_to", parent}, {"sender", A},
            {"ciphertext", data}};
        mime_roundtrip(mlsWire.dump(), XMailer::MLS_FILE_CHUNK);
        parent = id;
    }
    CHECK(received == file, "full-size and final chunks reconstruct identical file bytes");
}

static void test_tamper(const std::string& sid) {
    json m = encrypt(A, B, sid, "secret", "<t1>");

    json moved = m["message"];
    moved["x_reply_to"] = "<some-other-conversation>";
    CHECK(decrypt(B, moved)["status"] == "error", "x_reply_to is authenticated");

    json spoofed = m["message"];
    spoofed["from"] = "mallory@example.com";
    CHECK(decrypt(B, spoofed)["status"] == "error", "from is authenticated");

    json bitflip = m["message"];
    std::string ct = bitflip["body"]["ciphertext"];
    ct[ct.size() / 2] = (ct[ct.size() / 2] == 'A') ? 'B' : 'A';
    bitflip["body"]["ciphertext"] = ct;
    CHECK(decrypt(B, bitflip)["status"] == "error", "ciphertext is authenticated");

    // Untouched message still decrypts after the failed attempts (no slot consumed)
    json ok = decrypt(B, m["message"]);
    CHECK(ok["status"] == "success" && ok["plaintext"] == "secret", "original still decrypts");
}

static void test_bad_signature_and_identity_change() {
    std::vector<char> buf(1 << 20);
    const char* C = "carol@example.com";
    REQUIRE(signal_get_prekey_bundle(C, buf.data(), (int)buf.size()) == 0, "carol bundle");
    json cBundle = json::parse(buf.data());
    std::string sid = cBundle["session_id"];
    auto pb = cBundle["prekey_bundle"];

    // Forged SPK (signature does not match) is rejected
    std::string otherPub, otherPriv;
    generate_ecc_keypair(otherPub, otherPriv, "pw");
    int rc = signal_store_peer_prekey(B, C, sid.c_str(), pb["ik_pub"].get<std::string>().c_str(),
                                      otherPub.c_str(), pb["spk_sig"].get<std::string>().c_str());
    CHECK(rc == -2, "bad SPK signature rejected");

    // Genuine bundle pins Carol's IK for this session
    CHECK(store_bundle(B, C, cBundle), "carol pinned");
    char fp[128];
    signal_peer_identity_fingerprint(B, sid.c_str(), C, fp, sizeof(fp));
    CHECK(std::string(fp) == key_fingerprint(pb["ik_pub"]), "peer fingerprint matches bundle IK");

    // Same email, different identity key inside the same session: rejected until
    // the user resets trust. Other sessions are a clean slate (isolation).
    std::string ik2Pub, ik2Priv;
    generate_ecc_keypair(ik2Pub, ik2Priv, "pw2");
    std::string spk2Pub, spk2Priv;
    generate_ecc_keypair(spk2Pub, spk2Priv, "pw3");
    std::string sig2 = sign_with_ecc_private_key(ik2Priv, "pw2", spk2Pub);
    rc = signal_store_peer_prekey(B, C, sid.c_str(), ik2Pub.c_str(), spk2Pub.c_str(), sig2.c_str());
    CHECK(rc == -3, "changed identity rejected inside the session");
    rc = signal_store_peer_prekey(B, C, "sig_other", ik2Pub.c_str(), spk2Pub.c_str(), sig2.c_str());
    CHECK(rc == 0, "other session accepts a different peer IK (isolation)");

    CHECK(signal_peer_identity_reset(B, sid.c_str(), C) == 0, "reset trust");
    rc = signal_store_peer_prekey(B, C, sid.c_str(), ik2Pub.c_str(), spk2Pub.c_str(), sig2.c_str());
    CHECK(rc == 0, "new identity accepted after reset");
}

int main() {
    REQUIRE(sqlite3_open(":memory:", &g_db) == SQLITE_OK, "open memory db");
    email_db_create_signal_tables(g_db);
    test_wire_format();
    test_stored_wire_body();

    std::string initCt;
    std::string sid = handshake(initCt);
    test_bidirectional_and_out_of_order(sid);
    test_inline_file_chunks(sid);
    test_tamper(sid);
    test_bad_signature_and_identity_change();

    // Every session gets its own identity key (session isolation)
    char fp1[128], fp2[128];
    signal_identity_fingerprint(A, sid.c_str(), fp1, sizeof(fp1));
    std::string sid2 = handshake(initCt);
    signal_identity_fingerprint(A, sid2.c_str(), fp2, sizeof(fp2));
    CHECK(fp1[0] != '\0' && fp2[0] != '\0' && std::string(fp1) != fp2,
          "each session has its own IK");
    CHECK(sid2 != sid, "fresh session id");

    if (g_failures == 0) printf("signal_roundtrip_test: all checks passed\n");
    else printf("signal_roundtrip_test: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
