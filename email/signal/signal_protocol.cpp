#include "signal_protocol.h"
#include "email_core_common.h"
#include "signal_key_repo.h"
#include "signal_session_repo.h"
#include "logger.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/pem.h>
#include <cstring>
#include <vector>

static SignalKeyRepo s_keyRepo;
static SignalSessionRepo s_sessionRepo;

// Upper bound on message keys kept for out-of-order delivery per session.
static constexpr int kMaxSkip = 1000;

// --- Internal helpers ---

// HKDF-SHA256 extract+expand
static void hkdf_sha256(const std::string& ikm, const std::string& salt,
                        const std::string& info, uint8_t* out, size_t outLen) {
    uint8_t prk[SHA256_DIGEST_LENGTH];
    size_t prkLen = SHA256_DIGEST_LENGTH;
    EVP_MAC* hmac = EVP_MAC_fetch(NULL, "HMAC", NULL);
    EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(hmac);
    OSSL_PARAM params[2];
    params[0] = OSSL_PARAM_construct_utf8_string("digest", (char*)"SHA256", 0);
    params[1] = OSSL_PARAM_construct_end();

    EVP_MAC_init(ctx, (const uint8_t*)salt.data(), salt.size(), params);
    EVP_MAC_update(ctx, (const uint8_t*)ikm.data(), ikm.size());
    EVP_MAC_final(ctx, prk, &prkLen, sizeof(prk));

    uint8_t T[SHA256_DIGEST_LENGTH];
    size_t TLen = 0, done = 0;
    uint8_t counter = 1;
    while (done < outLen) {
        EVP_MAC_init(ctx, prk, prkLen, params);
        if (TLen > 0) EVP_MAC_update(ctx, T, TLen);
        EVP_MAC_update(ctx, (const uint8_t*)info.data(), info.size());
        EVP_MAC_update(ctx, &counter, 1);
        EVP_MAC_final(ctx, T, &TLen, sizeof(T));
        size_t toCopy = (outLen - done < TLen) ? (outLen - done) : TLen;
        memcpy(out + done, T, toCopy);
        done += toCopy;
        counter++;
    }
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(hmac);
}

static std::string to_base64(const uint8_t* data, size_t len) {
    return base64_encode(data, len);
}

static std::vector<uint8_t> from_base64(const std::string& b64) {
    return base64_decode(b64);
}

static std::string bytes_of(const std::vector<uint8_t>& v) {
    return std::string(v.begin(), v.end());
}

// Message key (32B) -> AES-256-GCM key (32B) || nonce (12B)
static void derive_aead_material(const std::string& mkB64, uint8_t key[32], uint8_t nonce[12]) {
    uint8_t out[44];
    hkdf_sha256(bytes_of(from_base64(mkB64)), std::string(32, '\0'), "OceanTalk-AEAD-v2", out, sizeof(out));
    memcpy(key, out, 32);
    memcpy(nonce, out + 32, 12);
}

// AES-256-GCM: returns base64(ciphertext || tag)
static std::string aead_encrypt(const std::string& mkB64, const std::string& plaintext,
                                const std::string& ad) {
    uint8_t key[32], nonce[12];
    derive_aead_material(mkB64, key, nonce);

    std::vector<uint8_t> out(plaintext.size() + 16);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len = 0, total = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
              EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) == 1 &&
              EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) == 1 &&
              EVP_EncryptUpdate(ctx, NULL, &len, (const uint8_t*)ad.data(), (int)ad.size()) == 1 &&
              EVP_EncryptUpdate(ctx, out.data(), &len, (const uint8_t*)plaintext.data(), (int)plaintext.size()) == 1;
    total = len;
    if (ok) ok = EVP_EncryptFinal_ex(ctx, out.data() + total, &len) == 1;
    total += len;
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, out.data() + total) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return "";
    return to_base64(out.data(), total + 16);
}

// Returns false on authentication failure.
static bool aead_decrypt(const std::string& mkB64, const std::string& ciphertextB64,
                         const std::string& ad, std::string& plaintext) {
    auto data = from_base64(ciphertextB64);
    if (data.size() < 16) return false;
    uint8_t key[32], nonce[12];
    derive_aead_material(mkB64, key, nonce);

    size_t ctLen = data.size() - 16;
    std::vector<uint8_t> out(ctLen + 1);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len = 0, total = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
              EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) == 1 &&
              EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) == 1 &&
              EVP_DecryptUpdate(ctx, NULL, &len, (const uint8_t*)ad.data(), (int)ad.size()) == 1 &&
              EVP_DecryptUpdate(ctx, out.data(), &len, data.data(), (int)ctLen) == 1;
    total = len;
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, data.data() + ctLen) == 1;
    if (ok) ok = EVP_DecryptFinal_ex(ctx, out.data() + total, &len) == 1;
    total += len;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return false;
    plaintext.assign((const char*)out.data(), total);
    return true;
}

// --- DH / KDF ---

std::string dh_compute_shared_secret(const std::string& ourPrivPem, const std::string& ourPassword,
                                     const std::string& peerPubPem) {
    BIO* privBio = BIO_new_mem_buf(ourPrivPem.data(), ourPrivPem.size());
    EVP_PKEY* ourKey = PEM_read_bio_PrivateKey(privBio, NULL, NULL, (void*)ourPassword.c_str());
    BIO_free(privBio);
    if (!ourKey) {
        LOG_INFO("[Signal] dh_compute: failed to load private key\n");
        return "";
    }

    BIO* pubBio = BIO_new_mem_buf(peerPubPem.data(), peerPubPem.size());
    EVP_PKEY* peerKey = PEM_read_bio_PUBKEY(pubBio, NULL, NULL, NULL);
    BIO_free(pubBio);
    if (!peerKey) {
        LOG_INFO("[Signal] dh_compute: failed to load peer public key\n");
        EVP_PKEY_free(ourKey);
        return "";
    }

    std::string secret;
    EVP_PKEY_CTX* derivCtx = EVP_PKEY_CTX_new(ourKey, NULL);
    if (derivCtx && EVP_PKEY_derive_init(derivCtx) > 0 &&
        EVP_PKEY_derive_set_peer(derivCtx, peerKey) > 0) {
        size_t secretLen = 0;
        EVP_PKEY_derive(derivCtx, NULL, &secretLen);
        std::vector<uint8_t> buf(secretLen);
        if (EVP_PKEY_derive(derivCtx, buf.data(), &secretLen) > 0) {
            secret.assign((const char*)buf.data(), secretLen);
        }
    }
    if (derivCtx) EVP_PKEY_CTX_free(derivCtx);
    EVP_PKEY_free(ourKey);
    EVP_PKEY_free(peerKey);
    return secret;
}

void kdf_rk_ck(const std::string& dhOutput, const std::string& oldRootKey,
               std::string& outRootKey, std::string& outChainKey) {
    uint8_t derived[64];
    std::string salt = oldRootKey.empty() ? std::string() : bytes_of(from_base64(oldRootKey));
    hkdf_sha256(dhOutput, salt, "DoubleRatchet", derived, 64);
    outRootKey = to_base64(derived, 32);
    outChainKey = to_base64(derived + 32, 32);
}

void kdf_mk(const std::string& chainKey, std::string& outMessageKey, std::string& outNewChainKey) {
    uint8_t derived[64];
    hkdf_sha256(bytes_of(from_base64(chainKey)), std::string("\x00", 1), "MessageKey", derived, 64);
    outMessageKey = to_base64(derived, 32);
    outNewChainKey = to_base64(derived + 32, 32);
}

static std::string x3dh_root_key(const std::string& dh1, const std::string& dh2, const std::string& dh3) {
    uint8_t rk[32];
    hkdf_sha256(dh1 + dh2 + dh3, std::string("\x00", 1), "X3DH_v1", rk, 32);
    return to_base64(rk, 32);
}

// --- Identity & prekeys ---

bool ensure_identity_key(const std::string& account) {
    SignalIdentityRecord rec;
    if (s_keyRepo.loadIdentity(account, rec)) return true;

    std::string pubPem, privPem;
    std::string password = generate_random_password(32);
    if (!generate_ecc_keypair(pubPem, privPem, password)) {
        LOG_INFO("[Signal] ensure_identity_key: failed to generate keypair for %s\n", account.c_str());
        return false;
    }
    if (!s_keyRepo.upsertIdentity(account, pubPem, privPem, password)) {
        LOG_INFO("[Signal] ensure_identity_key: failed to store identity for %s\n", account.c_str());
        return false;
    }
    LOG_INFO("[Signal] ensure_identity_key: created identity key for %s fp=%s\n",
             account.c_str(), key_fingerprint(pubPem).c_str());
    return true;
}

std::string identity_public_key(const std::string& account) {
    SignalIdentityRecord rec;
    return s_keyRepo.loadIdentity(account, rec) ? rec.ikPub : "";
}

std::string key_fingerprint(const std::string& pubPem) {
    if (pubPem.empty()) return "";
    uint8_t d[SHA256_DIGEST_LENGTH];
    SHA256((const uint8_t*)pubPem.data(), pubPem.size(), d);
    static const char* digits = "0123456789abcdef";
    std::string hex;
    for (uint8_t b : d) { hex += digits[b >> 4]; hex += digits[b & 0xF]; }
    return hex;
}

std::string sign_with_identity_key(const std::string& account, const std::string& data) {
    SignalIdentityRecord ik;
    if (!s_keyRepo.loadIdentity(account, ik)) return "";
    return sign_with_ecc_private_key(ik.ikPriv, ik.ikPassword, data);
}

int pin_peer_identity(const std::string& account, const std::string& peer, const std::string& ikPub) {
    SignalPeerIdentityRecord known;
    if (s_keyRepo.loadPeerIdentity(account, peer, known)) {
        if (known.ikPub == ikPub) return 0;
        LOG_INFO("[Signal] peer %s: identity key changed (known fp=%s, got fp=%s)\n",
                 peer.c_str(), key_fingerprint(known.ikPub).c_str(), key_fingerprint(ikPub).c_str());
        return -3;
    }
    s_keyRepo.upsertPeerIdentity(account, peer, ikPub);
    LOG_INFO("[Signal] peer %s: pinned identity key fp=%s\n", peer.c_str(), key_fingerprint(ikPub).c_str());
    return 0;
}

bool ensure_signed_prekey_for_session(const std::string& account, const std::string& sessionUuid) {
    SignalPrekeyRecord rec;
    if (s_keyRepo.loadSignedPrekey(account, sessionUuid, rec)) return true;

    SignalIdentityRecord ik;
    if (!s_keyRepo.loadIdentity(account, ik)) {
        LOG_INFO("[Signal] ensure_signed_prekey_for_session: no identity key for %s\n", account.c_str());
        return false;
    }

    std::string pubPem, privPem;
    std::string password = generate_random_password(32);
    if (!generate_ecc_keypair(pubPem, privPem, password)) {
        LOG_INFO("[Signal] ensure_signed_prekey_for_session: failed to generate keypair\n");
        return false;
    }
    std::string signature = sign_with_ecc_private_key(ik.ikPriv, ik.ikPassword, pubPem);
    if (signature.empty()) {
        LOG_INFO("[Signal] ensure_signed_prekey_for_session: failed to sign SPK\n");
        return false;
    }
    if (!s_keyRepo.insertSignedPrekey(account, sessionUuid, pubPem, privPem, password, signature)) {
        LOG_INFO("[Signal] ensure_signed_prekey_for_session: failed to store SPK for %s\n", sessionUuid.c_str());
        return false;
    }
    LOG_INFO("[Signal] ensure_signed_prekey_for_session: created SPK for session %s\n", sessionUuid.c_str());
    return true;
}

PrekeyBundle build_prekey_bundle_for_session(const std::string& account, const std::string& sessionUuid) {
    PrekeyBundle bundle;
    SignalIdentityRecord ik;
    SignalPrekeyRecord spk;
    if (!s_keyRepo.loadIdentity(account, ik) || !s_keyRepo.loadSignedPrekey(account, sessionUuid, spk)) {
        return bundle;
    }
    bundle.ikPub = ik.ikPub;
    bundle.spkPub = spk.pub;
    bundle.spkSig = spk.signature;
    return bundle;
}

bool verify_prekey_bundle(const PrekeyBundle& bundle) {
    if (bundle.ikPub.empty() || bundle.spkPub.empty() || bundle.spkSig.empty()) return false;
    return verify_with_ecc_public_key(bundle.ikPub, bundle.spkPub, bundle.spkSig);
}

std::string generate_session_id() {
    unsigned char buf[16];
    RAND_bytes(buf, 16);
    static const char* digits = "0123456789abcdef";
    std::string hex;
    for (int i = 0; i < 16; i++) {
        hex += digits[(buf[i] >> 4) & 0xF];
        hex += digits[buf[i] & 0xF];
    }
    return "sig_" + hex;
}

// --- X3DH ---

bool x3dh_initiate(const std::string& account,
                   const PrekeyBundle& peerBundle,
                   std::string& outRootKey,
                   std::string& outEkPub,
                   std::string& outEkPriv,
                   std::string& outEkPassword) {
    SignalIdentityRecord ik;
    if (!s_keyRepo.loadIdentity(account, ik)) {
        LOG_INFO("[Signal] x3dh_initiate: no identity key for %s\n", account.c_str());
        return false;
    }

    outEkPassword = generate_random_password(32);
    if (!generate_ecc_keypair(outEkPub, outEkPriv, outEkPassword)) {
        LOG_INFO("[Signal] x3dh_initiate: failed to generate ephemeral key\n");
        return false;
    }

    // DH1 = DH(IK_A, SPK_B), DH2 = DH(EK_A, IK_B), DH3 = DH(EK_A, SPK_B)
    std::string dh1 = dh_compute_shared_secret(ik.ikPriv, ik.ikPassword, peerBundle.spkPub);
    std::string dh2 = dh_compute_shared_secret(outEkPriv, outEkPassword, peerBundle.ikPub);
    std::string dh3 = dh_compute_shared_secret(outEkPriv, outEkPassword, peerBundle.spkPub);
    if (dh1.empty() || dh2.empty() || dh3.empty()) return false;

    outRootKey = x3dh_root_key(dh1, dh2, dh3);
    return true;
}

bool x3dh_respond(const std::string& account,
                  const std::string& sessionUuid,
                  const std::string& peerIkPub,
                  const std::string& peerEkPub,
                  std::string& outRootKey) {
    SignalIdentityRecord ik;
    if (!s_keyRepo.loadIdentity(account, ik)) {
        LOG_INFO("[Signal] x3dh_respond: no identity key for %s\n", account.c_str());
        return false;
    }
    SignalPrekeyRecord spk;
    if (!s_keyRepo.loadSignedPrekey(account, sessionUuid, spk)) {
        LOG_INFO("[Signal] x3dh_respond: no signed prekey for session %s\n", sessionUuid.c_str());
        return false;
    }

    // DH1 = DH(SPK_B, IK_A), DH2 = DH(IK_B, EK_A), DH3 = DH(SPK_B, EK_A)
    std::string dh1 = dh_compute_shared_secret(spk.priv, spk.password, peerIkPub);
    std::string dh2 = dh_compute_shared_secret(ik.ikPriv, ik.ikPassword, peerEkPub);
    std::string dh3 = dh_compute_shared_secret(spk.priv, spk.password, peerEkPub);
    if (dh1.empty() || dh2.empty() || dh3.empty()) return false;

    outRootKey = x3dh_root_key(dh1, dh2, dh3);
    return true;
}

// --- Double Ratchet ---

bool dr_initialize(const std::string& account, const std::string& peerEmail,
                   const std::string& sessionId, const std::string& rootKey,
                   const std::string& peerDhPub, bool isInitiator) {
    SignalSessionRecord rec;
    rec.account = account;
    rec.peerEmail = peerEmail;
    rec.sessionId = sessionId;
    rec.rootKey = rootKey;

    if (isInitiator) {
        // Alice: DHs = fresh pair, DHr = Bob's SPK, (RK, CKs) = KDF_RK(RK, DH(DHs, DHr))
        rec.dhPeerPub = peerDhPub;
        std::string dhPub, dhPriv, dhPassword = generate_random_password(32);
        if (!generate_ecc_keypair(dhPub, dhPriv, dhPassword)) {
            LOG_INFO("[Signal] dr_init: failed to generate DH keypair\n");
            return false;
        }
        rec.dhSelfPub = dhPub;
        rec.dhSelfPriv = dhPriv;
        rec.dhSelfPassword = dhPassword;

        std::string dhOut = dh_compute_shared_secret(dhPriv, dhPassword, peerDhPub);
        if (dhOut.empty()) return false;
        std::string newRk, ck;
        kdf_rk_ck(dhOut, rootKey, newRk, ck);
        rec.rootKey = newRk;
        rec.sendChainKey = ck;
    } else {
        // Bob: DHs = own SPK for this session; the first incoming message carries
        // Alice's ratchet key and triggers the receiving-side DH step.
        SignalPrekeyRecord spk;
        if (!s_keyRepo.loadSignedPrekey(account, sessionId, spk)) {
            LOG_INFO("[Signal] dr_init: no signed prekey for responder session %s\n", sessionId.c_str());
            return false;
        }
        rec.dhSelfPub = spk.pub;
        rec.dhSelfPriv = spk.priv;
        rec.dhSelfPassword = spk.password;
    }

    return s_sessionRepo.saveSession(rec);
}

bool dr_encrypt(const std::string& account, const std::string& peerEmail,
                const std::string& sessionId, const std::string& plaintext,
                const AdBuilder& adBuilder, EncryptedMessage& outMsg) {
    SignalSessionRecord rec;
    if (!s_sessionRepo.loadSession(account, peerEmail, sessionId, rec)) {
        LOG_INFO("[Signal] dr_encrypt: session not found\n");
        return false;
    }

    if (rec.sendChainKey.empty()) {
        // Sending half of the DH ratchet step: new DHs, (RK, CKs) = KDF_RK(RK, DH(DHs, DHr)).
        // prevSendN / sendN were already rotated when the peer's new key arrived.
        std::string dhPub, dhPriv, dhPassword = generate_random_password(32);
        if (!generate_ecc_keypair(dhPub, dhPriv, dhPassword)) {
            LOG_INFO("[Signal] dr_encrypt: failed to generate DH keypair\n");
            return false;
        }
        std::string dhOut = dh_compute_shared_secret(dhPriv, dhPassword, rec.dhPeerPub);
        if (dhOut.empty()) return false;
        std::string newRk, ck;
        kdf_rk_ck(dhOut, rec.rootKey, newRk, ck);
        rec.rootKey = newRk;
        rec.sendChainKey = ck;
        rec.dhSelfPub = dhPub;
        rec.dhSelfPriv = dhPriv;
        rec.dhSelfPassword = dhPassword;
    }

    std::string mk, newCk;
    kdf_mk(rec.sendChainKey, mk, newCk);

    outMsg.header.dhPub = rec.dhSelfPub;
    outMsg.header.n = rec.sendN;
    outMsg.header.pn = rec.prevSendN;
    if (outMsg.header.msgType.empty()) outMsg.header.msgType = "msg";

    outMsg.ciphertext = aead_encrypt(mk, plaintext, adBuilder(outMsg.header));
    if (outMsg.ciphertext.empty()) {
        LOG_INFO("[Signal] dr_encrypt: AEAD failed for session=%s\n", sessionId.c_str());
        return false;
    }

    rec.sendChainKey = newCk;
    rec.sendN++;
    if (!s_sessionRepo.saveSession(rec)) return false;

    LOG_INFO("[Signal] dr_encrypt: n=%d pn=%d session=%s\n",
             outMsg.header.n, outMsg.header.pn, sessionId.c_str());
    return true;
}

namespace {
struct SkippedKey {
    std::string dhPub;
    int n;
    std::string mk;
};

// Advance chainKey to `until`, collecting message keys for the skipped indices.
// Returns false when the gap exceeds kMaxSkip.
bool skip_message_keys(std::string& chainKey, int& n, int until, const std::string& dhPub,
                       std::vector<SkippedKey>& out) {
    if (chainKey.empty()) return true;
    if (until - n > kMaxSkip) return false;
    while (n < until) {
        std::string mk, newCk;
        kdf_mk(chainKey, mk, newCk);
        out.push_back({dhPub, n, mk});
        chainKey = newCk;
        n++;
    }
    return true;
}
} // namespace

DecryptResult dr_decrypt(const std::string& account, const std::string& peerEmail,
                         const std::string& sessionId, const EncryptedMessage& msg,
                         const std::string& ad) {
    DecryptResult result;
    result.sessionId = sessionId;

    SignalSessionRecord rec;
    if (!s_sessionRepo.loadSession(account, peerEmail, sessionId, rec)) {
        result.error = "session_not_found";
        return result;
    }

    // 1. Message key kept from an earlier gap?
    SignalSkippedKeyRecord skippedRec;
    if (s_sessionRepo.findSkippedKey(sessionId, msg.header.dhPub, msg.header.n, skippedRec)) {
        if (aead_decrypt(skippedRec.messageKey, msg.ciphertext, ad, result.plaintext)) {
            s_sessionRepo.deleteSkippedKey(sessionId, msg.header.dhPub, msg.header.n);
            result.success = true;
        } else {
            result.error = "decrypt_failed_skipped";
        }
        return result;
    }

    // All state changes stay local until the ciphertext authenticates.
    std::vector<SkippedKey> skipped;

    // 2. New ratchet key from the peer -> DH ratchet step
    if (msg.header.dhPub != rec.dhPeerPub) {
        // Keys for messages still outstanding on the old receiving chain (up to pn).
        if (!skip_message_keys(rec.recvChainKey, rec.recvN, msg.header.pn, rec.dhPeerPub, skipped)) {
            result.error = "too_many_skipped";
            return result;
        }
        // PN = Ns, Ns = Nr = 0, DHr = header.dh, (RK, CKr) = KDF_RK(RK, DH(DHs, DHr)).
        // The new DHs / CKs are derived lazily on the next send (dr_encrypt).
        rec.prevSendN = rec.sendN;
        rec.sendN = 0;
        rec.sendChainKey.clear();
        rec.dhPeerPub = msg.header.dhPub;
        rec.recvN = 0;

        std::string dhOut = dh_compute_shared_secret(rec.dhSelfPriv, rec.dhSelfPassword, rec.dhPeerPub);
        if (dhOut.empty()) {
            result.error = "dh_failed";
            return result;
        }
        std::string newRk, ck;
        kdf_rk_ck(dhOut, rec.rootKey, newRk, ck);
        rec.rootKey = newRk;
        rec.recvChainKey = ck;
    }

    // 3. Skip ahead on the current receiving chain
    if (!skip_message_keys(rec.recvChainKey, rec.recvN, msg.header.n, rec.dhPeerPub, skipped)) {
        result.error = "too_many_skipped";
        return result;
    }

    // 4. Decrypt with the next message key
    std::string mk, newCk;
    kdf_mk(rec.recvChainKey, mk, newCk);
    if (!aead_decrypt(mk, msg.ciphertext, ad, result.plaintext)) {
        // Nothing persisted: a failed decrypt must not consume a chain slot.
        result.error = "decrypt_failed";
        LOG_INFO("[Signal] dr_decrypt: n=%d session=%s success=0\n", msg.header.n, sessionId.c_str());
        return result;
    }

    rec.recvChainKey = newCk;
    rec.recvN++;
    for (auto& k : skipped) {
        s_sessionRepo.insertSkippedKey(sessionId, k.dhPub, k.n, k.mk);
    }
    s_sessionRepo.saveSession(rec);
    result.success = true;

    LOG_INFO("[Signal] dr_decrypt: n=%d session=%s success=1 skipped=%zu\n",
             msg.header.n, sessionId.c_str(), skipped.size());
    return result;
}
