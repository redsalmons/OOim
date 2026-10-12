// Two-party Signal (X3DH + Double Ratchet, wire v2) round trip through the C API.
// Alice and Bob share one in-memory database; their state is separated by account.
#include "email_core.h"
#include "email_core_common.h"
#include "signal_protocol.h"
#include "signal_message.h"
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using json = nlohmann::json;

static int g_failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); g_failures++; } } while (0)
#define REQUIRE(cond, msg) do { if (!(cond)) { fprintf(stderr, "FATAL %s:%d: %s\n", __FILE__, __LINE__, msg); exit(1); } } while (0)

static const char* A = "alice@example.com";
static const char* B = "bob@example.com";

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
    std::string body = message.dump();
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

    // Genuine bundle pins Carol's IK
    CHECK(store_bundle(B, C, cBundle), "carol pinned");
    char fp[128];
    signal_peer_identity_fingerprint(B, C, fp, sizeof(fp));
    CHECK(std::string(fp) == key_fingerprint(pb["ik_pub"]), "peer fingerprint matches bundle IK");

    // Same email, different identity key: rejected until the user resets trust
    std::string ik2Pub, ik2Priv;
    generate_ecc_keypair(ik2Pub, ik2Priv, "pw2");
    std::string spk2Pub, spk2Priv;
    generate_ecc_keypair(spk2Pub, spk2Priv, "pw3");
    std::string sig2 = sign_with_ecc_private_key(ik2Priv, "pw2", spk2Pub);
    rc = signal_store_peer_prekey(B, C, "sig_other", ik2Pub.c_str(), spk2Pub.c_str(), sig2.c_str());
    CHECK(rc == -3, "changed identity rejected");

    CHECK(signal_peer_identity_reset(B, C) == 0, "reset trust");
    rc = signal_store_peer_prekey(B, C, "sig_other", ik2Pub.c_str(), spk2Pub.c_str(), sig2.c_str());
    CHECK(rc == 0, "new identity accepted after reset");
}

int main() {
    REQUIRE(sqlite3_open(":memory:", &g_db) == SQLITE_OK, "open memory db");
    email_db_create_signal_tables(g_db);

    std::string initCt;
    std::string sid = handshake(initCt);
    test_bidirectional_and_out_of_order(sid);
    test_tamper(sid);
    test_bad_signature_and_identity_change();

    // Account identity key is stable across sessions
    char fp1[128], fp2[128];
    signal_identity_fingerprint(A, fp1, sizeof(fp1));
    std::string sid2 = handshake(initCt);
    signal_identity_fingerprint(A, fp2, sizeof(fp2));
    CHECK(std::string(fp1) == fp2 && fp1[0] != '\0', "account IK is long-lived");
    CHECK(sid2 != sid, "fresh session id");

    if (g_failures == 0) printf("signal_roundtrip_test: all checks passed\n");
    else printf("signal_roundtrip_test: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
