#include "signal_protocol.h"
#include "email_core_common.h"
#include "signal_key_repo.h"
#include "signal_session_repo.h"
#include "logger.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>
#include <openssl/pem.h>
#include <cstring>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static SignalKeyRepo s_keyRepo;
static SignalSessionRepo s_sessionRepo;

// --- Internal helpers ---

// HKDF-SHA256: extract+expand to derive 64 bytes (32 root_key + 32 chain_key)
static void hkdf_sha256(const std::string& ikm, const std::string& salt,
                        const std::string& info, uint8_t* out, size_t outLen) {
    // Extract: PRK = HMAC-SHA256(salt, IKM)
    uint8_t prk[SHA256_DIGEST_LENGTH];
    size_t prkLen = SHA256_DIGEST_LENGTH;
    EVP_MAC* hmac = EVP_MAC_fetch(NULL, "HMAC", NULL);
    EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(hmac);
    OSSL_PARAM params[2];
    params[0] = OSSL_PARAM_construct_utf8_string("digest", (char*)"SHA256", 0);
    params[1] = OSSL_PARAM_construct_end();

    // Extract
    EVP_MAC_init(ctx, (const uint8_t*)salt.data(), salt.size(), params);
    EVP_MAC_update(ctx, (const uint8_t*)ikm.data(), ikm.size());
    EVP_MAC_final(ctx, prk, &prkLen, sizeof(prk));

    // Expand: T(0) = empty, T(1) = HMAC(PRK, T(0) | info | 0x01), ...
    uint8_t T[SHA256_DIGEST_LENGTH];
    size_t TLen = 0;
    size_t done = 0;
    uint8_t counter = 1;

    while (done < outLen) {
        EVP_MAC_init(ctx, prk, prkLen, params);
        if (TLen > 0) {
            EVP_MAC_update(ctx, T, TLen);
        }
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

static std::string to_base64(const std::string& s) {
    return base64_encode((const unsigned char*)s.data(), s.size());
}

static std::vector<uint8_t> from_base64(const std::string& b64) {
    return base64_decode(b64);
}

// AES-256-CBC encrypt with random IV, return base64(IV + ciphertext)
static std::string aes_encrypt(const std::string& key32, const std::string& plaintext) {
    unsigned char iv[16];
    RAND_bytes(iv, 16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL,
                       (const unsigned char*)key32.data(), iv);

    std::vector<uint8_t> output(16 + plaintext.size() + 16);
    memcpy(output.data(), iv, 16);

    int len;
    int ciphertextLen = 0;
    EVP_EncryptUpdate(ctx, output.data() + 16, &len,
                      (const unsigned char*)plaintext.data(), plaintext.size());
    ciphertextLen = len;
    EVP_EncryptFinal_ex(ctx, output.data() + 16 + len, &len);
    ciphertextLen += len;
    output.resize(16 + ciphertextLen);
    EVP_CIPHER_CTX_free(ctx);

    return to_base64(output.data(), output.size());
}

// AES-256-CBC decrypt: input is base64(IV + ciphertext)
static std::string aes_decrypt(const std::string& key32, const std::string& ciphertextB64) {
    auto data = from_base64(ciphertextB64);
    if (data.size() < 16) return "";

    const unsigned char* iv = data.data();
    const unsigned char* ct = data.data() + 16;
    int ctLen = data.size() - 16;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL,
                       (const unsigned char*)key32.data(), iv);

    std::vector<uint8_t> output(ctLen + 16);
    int len, plaintextLen = 0;
    EVP_DecryptUpdate(ctx, output.data(), &len, ct, ctLen);
    plaintextLen = len;
    if (EVP_DecryptFinal_ex(ctx, output.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    plaintextLen += len;
    output.resize(plaintextLen);
    EVP_CIPHER_CTX_free(ctx);

    return std::string((char*)output.data(), output.size());
}

// --- DH operations ---

std::string dh_compute_shared_secret(const std::string& ourPrivPem, const std::string& ourPassword,
                                     const std::string& peerPubPem) {
    // Use ECDH with the existing ECC key infrastructure
    // We use ecc_decrypt/encrypt as a proxy for ECDH since we already have ECC keys
    // Actually, we need raw ECDH. Let's use OpenSSL directly.
    
    // Load our private key
    BIO* privBio = BIO_new_mem_buf(ourPrivPem.data(), ourPrivPem.size());
    EVP_PKEY* ourKey = PEM_read_bio_PrivateKey(privBio, NULL, NULL, (void*)ourPassword.c_str());
    BIO_free(privBio);
    if (!ourKey) {
        LOG_INFO("[Signal] dh_compute: failed to load private key\n");
        return "";
    }

    // Load peer public key
    BIO* pubBio = BIO_new_mem_buf(peerPubPem.data(), peerPubPem.size());
    EVP_PKEY* peerKey = PEM_read_bio_PUBKEY(pubBio, NULL, NULL, NULL);
    BIO_free(pubBio);
    if (!peerKey) {
        LOG_INFO("[Signal] dh_compute: failed to load peer public key\n");
        EVP_PKEY_free(ourKey);
        return "";
    }

    // Derive shared secret
    EVP_PKEY_CTX* derivCtx = EVP_PKEY_CTX_new(ourKey, NULL);
    if (!derivCtx) {
        EVP_PKEY_free(ourKey);
        EVP_PKEY_free(peerKey);
        return "";
    }

    if (EVP_PKEY_derive_init(derivCtx) <= 0 ||
        EVP_PKEY_derive_set_peer(derivCtx, peerKey) <= 0) {
        EVP_PKEY_CTX_free(derivCtx);
        EVP_PKEY_free(ourKey);
        EVP_PKEY_free(peerKey);
        return "";
    }

    size_t secretLen = 0;
    EVP_PKEY_derive(derivCtx, NULL, &secretLen);
    std::vector<uint8_t> sharedSecret(secretLen);
    if (EVP_PKEY_derive(derivCtx, sharedSecret.data(), &secretLen) <= 0) {
        secretLen = 0;
    }
    sharedSecret.resize(secretLen);

    EVP_PKEY_CTX_free(derivCtx);
    EVP_PKEY_free(ourKey);
    EVP_PKEY_free(peerKey);

    return std::string((char*)sharedSecret.data(), sharedSecret.size());
}

void kdf_rk_ck(const std::string& dhOutput, const std::string& oldRootKey,
               std::string& outRootKey, std::string& outChainKey) {
    uint8_t derived[64];
    // Use old root key (decoded from base64) as HKDF salt — standard Double Ratchet KDF_RK
    std::string salt;
    if (!oldRootKey.empty()) {
        auto rkBytes = from_base64(oldRootKey);
        salt.assign((const char*)rkBytes.data(), rkBytes.size());
    }
    std::string info = "DoubleRatchet";
    hkdf_sha256(dhOutput, salt, info, derived, 64);
    outRootKey = to_base64(derived, 32);
    outChainKey = to_base64(derived + 32, 32);
}

void kdf_mk(const std::string& chainKey, std::string& outMessageKey, std::string& outNewChainKey) {
    // Derive message key and advance chain key using HKDF
    auto ckBytes = from_base64(chainKey);
    
    uint8_t derived[64];
    std::string salt(ckBytes.begin(), ckBytes.end());
    std::string info = "MessageKey";
    hkdf_sha256(salt, std::string("\x00", 1), info, derived, 64);
    
    outMessageKey = to_base64(derived, 32);
    outNewChainKey = to_base64(derived + 32, 32);
}

// --- Identity & Prekey management ---

bool ensure_identity_key(const std::string& account) {
    SignalIdentityRecord rec;
    if (s_keyRepo.loadIdentity(account, rec)) {
        return true;  // Already exists
    }

    // Generate new identity key
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

    LOG_INFO("[Signal] ensure_identity_key: created identity key for %s\n", account.c_str());
    return true;
}

bool ensure_signed_prekey(const std::string& account) {
    SignalPrekeyRecord rec;
    if (s_keyRepo.loadSignedPrekey(account, rec)) {
        return true;  // Already exists
    }

    // Generate new signed prekey
    std::string pubPem, privPem;
    std::string password = generate_random_password(32);
    if (!generate_ecc_keypair(pubPem, privPem, password)) {
        LOG_INFO("[Signal] ensure_signed_prekey: failed to generate keypair\n");
        return false;
    }

    // Sign the SPK with identity key
    SignalIdentityRecord ikRec;
    if (!s_keyRepo.loadIdentity(account, ikRec)) {
        LOG_INFO("[Signal] ensure_signed_prekey: no identity key\n");
        return false;
    }

    std::string signature = sign_with_ecc_private_key(ikRec.ikPriv, ikRec.ikPassword, pubPem);

    if (!s_keyRepo.insertSignedPrekey(account, pubPem, privPem, password, signature)) {
        LOG_INFO("[Signal] ensure_signed_prekey: failed to store\n");
        return false;
    }

    LOG_INFO("[Signal] ensure_signed_prekey: created signed prekey for %s\n", account.c_str());
    return true;
}

bool ensure_one_time_prekeys(const std::string& account, int minCount) {
    int available = s_keyRepo.countAvailableOneTimePrekeys(account);
    if (available >= minCount) return true;

    int toGenerate = minCount - available;
    for (int i = 0; i < toGenerate; i++) {
        std::string pubPem, privPem;
        std::string password = generate_random_password(32);
        if (!generate_ecc_keypair(pubPem, privPem, password)) {
            LOG_INFO("[Signal] ensure_opk: failed to generate keypair %d\n", i);
            continue;
        }
        s_keyRepo.insertOneTimePrekey(account, pubPem, privPem, password);
    }

    LOG_INFO("[Signal] ensure_opk: generated %d OPKs for %s\n", toGenerate, account.c_str());
    return true;
}

// Per-session variants use sessionUuid as logical account key in SignalKeyRepo
bool ensure_identity_key_for_session(const std::string& sessionUuid) {
    SignalIdentityRecord rec;
    if (s_keyRepo.loadIdentityForSession(sessionUuid, rec)) {
        return true;
    }

    std::string pubPem, privPem;
    std::string password = generate_random_password(32);
    if (!generate_ecc_keypair(pubPem, privPem, password)) {
        LOG_INFO("[Signal] ensure_identity_key_for_session: failed to generate keypair for %s\n", sessionUuid.c_str());
        return false;
    }

    if (!s_keyRepo.upsertIdentityForSession(sessionUuid, pubPem, privPem, password)) {
        LOG_INFO("[Signal] ensure_identity_key_for_session: failed to store identity for %s\n", sessionUuid.c_str());
        return false;
    }

    LOG_INFO("[Signal] ensure_identity_key_for_session: created identity key for session %s\n", sessionUuid.c_str());
    return true;
}

bool ensure_signed_prekey_for_session(const std::string& sessionUuid) {
    SignalPrekeyRecord rec;
    if (s_keyRepo.loadSignedPrekeyForSession(sessionUuid, rec)) {
        return true;
    }

    std::string pubPem, privPem;
    std::string password = generate_random_password(32);
    if (!generate_ecc_keypair(pubPem, privPem, password)) {
        LOG_INFO("[Signal] ensure_signed_prekey_for_session: failed to generate keypair\n");
        return false;
    }

    // Ensure we have a per-session identity key and load it
    if (!ensure_identity_key_for_session(sessionUuid)) {
        LOG_INFO("[Signal] ensure_signed_prekey_for_session: no identity key for session %s\n", sessionUuid.c_str());
        return false;
    }

    SignalIdentityRecord ikRec;
    if (!s_keyRepo.loadIdentityForSession(sessionUuid, ikRec)) {
        LOG_INFO("[Signal] ensure_signed_prekey_for_session: failed to load identity for session %s\n", sessionUuid.c_str());
        return false;
    }

    std::string signature = sign_with_ecc_private_key(ikRec.ikPriv, ikRec.ikPassword, pubPem);

    if (!s_keyRepo.insertSignedPrekeyForSession(sessionUuid, pubPem, privPem, password, signature)) {
        LOG_INFO("[Signal] ensure_signed_prekey_for_session: failed to store SPK for session %s\n", sessionUuid.c_str());
        return false;
    }

    LOG_INFO("[Signal] ensure_signed_prekey_for_session: created signed prekey for session %s\n", sessionUuid.c_str());
    return true;
}

bool ensure_one_time_prekeys_for_session(const std::string& sessionUuid, int minCount) {
    int available = s_keyRepo.countAvailableOneTimePrekeys(sessionUuid);
    if (available >= minCount) return true;

    int toGenerate = minCount - available;
    for (int i = 0; i < toGenerate; i++) {
        std::string pubPem, privPem;
        std::string password = generate_random_password(32);
        if (!generate_ecc_keypair(pubPem, privPem, password)) {
            LOG_INFO("[Signal] ensure_opk_for_session: failed to generate keypair %d for session %s\n", i, sessionUuid.c_str());
            continue;
        }
        s_keyRepo.insertOneTimePrekeyForSession(sessionUuid, pubPem, privPem, password);
    }

    LOG_INFO("[Signal] ensure_opk_for_session: generated %d OPKs for session %s\n", toGenerate, sessionUuid.c_str());
    return true;
}

PrekeyBundle build_prekey_bundle(const std::string& account) {
    PrekeyBundle bundle;
    
    SignalIdentityRecord ikRec;
    if (!s_keyRepo.loadIdentity(account, ikRec)) return bundle;
    bundle.ikPub = ikRec.ikPub;

    SignalPrekeyRecord spkRec;
    if (!s_keyRepo.loadSignedPrekey(account, spkRec)) return bundle;
    bundle.spkPub = spkRec.pub;
    bundle.spkSig = spkRec.signature;

    // Don't include OPK — x3dh_respond doesn't compute DH4, so including it
    // would cause root key mismatch between initiator and responder
    bundle.opkPub = "";

    return bundle;
}

PrekeyBundle build_prekey_bundle_for_session(const std::string& sessionUuid) {
    PrekeyBundle bundle;

    SignalIdentityRecord ikRec;
    if (!s_keyRepo.loadIdentityForSession(sessionUuid, ikRec)) return bundle;
    bundle.ikPub = ikRec.ikPub;

    SignalPrekeyRecord spkRec;
    if (!s_keyRepo.loadSignedPrekeyForSession(sessionUuid, spkRec)) return bundle;
    bundle.spkPub = spkRec.pub;
    bundle.spkSig = spkRec.signature;

    bundle.opkPub = "";

    return bundle;
}

std::string generate_session_id() {
    unsigned char buf[16];
    RAND_bytes(buf, 16);
    std::string hex;
    static const char* digits = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        hex += digits[(buf[i] >> 4) & 0xF];
        hex += digits[buf[i] & 0xF];
    }
    return "sig_" + hex;
}

// --- X3DH ---

bool x3dh_initiate(const std::string& aliceAccount,
                   const PrekeyBundle& bobBundle,
                   std::string& outRootKey,
                   std::string& outEkPub,
                   std::string& outEkPriv,
                   std::string& outEkPassword) {
    // Load Alice's identity key
    SignalIdentityRecord aliceIk;
    if (!s_keyRepo.loadIdentity(aliceAccount, aliceIk)) {
        LOG_INFO("[Signal] x3dh_initiate: no identity key for %s\n", aliceAccount.c_str());
        return false;
    }

    // Generate ephemeral key
    outEkPassword = generate_random_password(32);
    if (!generate_ecc_keypair(outEkPub, outEkPriv, outEkPassword)) {
        LOG_INFO("[Signal] x3dh_initiate: failed to generate ephemeral key\n");
        return false;
    }

    // Compute DH outputs:
    // DH1 = DH(IK_A, SPK_B)
    // DH2 = DH(EK_A, IK_B)
    // DH3 = DH(EK_A, SPK_B)
    // DH4 = DH(EK_A, OPK_B)  [if OPK available]

    std::string dh1 = dh_compute_shared_secret(aliceIk.ikPriv, aliceIk.ikPassword, bobBundle.spkPub);
    std::string dh2 = dh_compute_shared_secret(outEkPriv, outEkPassword, bobBundle.ikPub);
    std::string dh3 = dh_compute_shared_secret(outEkPriv, outEkPassword, bobBundle.spkPub);

    std::string combined = dh1 + dh2 + dh3;
    if (!bobBundle.opkPub.empty()) {
        std::string dh4 = dh_compute_shared_secret(outEkPriv, outEkPassword, bobBundle.opkPub);
        combined += dh4;
    }

    // Derive root key: RK = HKDF(combined, salt="", info="X3DH_v1")
    uint8_t rootKeyBytes[32];
    hkdf_sha256(combined, std::string("\x00", 1), "X3DH_v1", rootKeyBytes, 32);
    outRootKey = to_base64(rootKeyBytes, 32);

    LOG_INFO("[Signal] x3dh_initiate: root key computed for %s -> %s\n",
             aliceAccount.c_str(), bobBundle.ikPub.substr(0, 20).c_str());
    return true;
}

bool x3dh_respond(const std::string& bobAccount,
                  const std::string& aliceIkPub,
                  const std::string& aliceEkPub,
                  std::string& outRootKey) {
    // Load Bob's identity key, signed prekey, and (optionally) one-time prekey
    SignalIdentityRecord bobIk;
    if (!s_keyRepo.loadIdentity(bobAccount, bobIk)) {
        LOG_INFO("[Signal] x3dh_respond: no identity key for %s\n", bobAccount.c_str());
        return false;
    }

    SignalPrekeyRecord bobSpk;
    if (!s_keyRepo.loadSignedPrekey(bobAccount, bobSpk)) {
        LOG_INFO("[Signal] x3dh_respond: no signed prekey for %s\n", bobAccount.c_str());
        return false;
    }

    // Compute DH outputs (responder side):
    // DH1 = DH(SPK_B, IK_A)
    // DH2 = DH(IK_B, EK_A)
    // DH3 = DH(SPK_B, EK_A)
    // DH4 = DH(OPK_B, EK_A)  [if OPK was used]

    std::string dh1 = dh_compute_shared_secret(bobSpk.priv, bobSpk.password, aliceIkPub);
    std::string dh2 = dh_compute_shared_secret(bobIk.ikPriv, bobIk.ikPassword, aliceEkPub);
    std::string dh3 = dh_compute_shared_secret(bobSpk.priv, bobSpk.password, aliceEkPub);

    std::string combined = dh1 + dh2 + dh3;
    // Note: OPK handling would need to know which OPK was used by Alice
    // For now, we try without OPK (simplified X3DH without one-time prekeys)
    // TODO: Add OPK support when the init message carries the OPK ID

    uint8_t rootKeyBytes[32];
    hkdf_sha256(combined, std::string("\x00", 1), "X3DH_v1", rootKeyBytes, 32);
    outRootKey = to_base64(rootKeyBytes, 32);

    LOG_INFO("[Signal] x3dh_respond: root key computed for %s\n", bobAccount.c_str());
    return true;
}

bool x3dh_initiate_for_session(const std::string& sessionUuid,
                                const PrekeyBundle& bobBundle,
                                std::string& outRootKey,
                                std::string& outEkPub,
                                std::string& outEkPriv,
                                std::string& outEkPassword) {
    SignalIdentityRecord aliceIk;
    if (!s_keyRepo.loadIdentityForSession(sessionUuid, aliceIk)) {
        LOG_INFO("[Signal] x3dh_initiate_for_session: no identity key for session %s\n", sessionUuid.c_str());
        return false;
    }

    outEkPassword = generate_random_password(32);
    if (!generate_ecc_keypair(outEkPub, outEkPriv, outEkPassword)) {
        LOG_INFO("[Signal] x3dh_initiate_for_session: failed to generate ephemeral key\n");
        return false;
    }

    std::string dh1 = dh_compute_shared_secret(aliceIk.ikPriv, aliceIk.ikPassword, bobBundle.spkPub);
    std::string dh2 = dh_compute_shared_secret(outEkPriv, outEkPassword, bobBundle.ikPub);
    std::string dh3 = dh_compute_shared_secret(outEkPriv, outEkPassword, bobBundle.spkPub);

    std::string combined = dh1 + dh2 + dh3;
    if (!bobBundle.opkPub.empty()) {
        std::string dh4 = dh_compute_shared_secret(outEkPriv, outEkPassword, bobBundle.opkPub);
        combined += dh4;
    }

    uint8_t rootKeyBytes[32];
    hkdf_sha256(combined, std::string("\x00", 1), "X3DH_v1", rootKeyBytes, 32);
    outRootKey = to_base64(rootKeyBytes, 32);

    LOG_INFO("[Signal] x3dh_initiate_for_session: root key computed for session %s\n", sessionUuid.c_str());
    return true;
}

bool x3dh_respond_for_session(const std::string& sessionUuid,
                               const std::string& aliceIkPub,
                               const std::string& aliceEkPub,
                               std::string& outRootKey) {
    SignalIdentityRecord bobIk;
    if (!s_keyRepo.loadIdentityForSession(sessionUuid, bobIk)) {
        LOG_INFO("[Signal] x3dh_respond_for_session: no identity key for session %s\n", sessionUuid.c_str());
        return false;
    }

    SignalPrekeyRecord bobSpk;
    if (!s_keyRepo.loadSignedPrekeyForSession(sessionUuid, bobSpk)) {
        LOG_INFO("[Signal] x3dh_respond_for_session: no signed prekey for session %s\n", sessionUuid.c_str());
        return false;
    }

    std::string dh1 = dh_compute_shared_secret(bobSpk.priv, bobSpk.password, aliceIkPub);
    std::string dh2 = dh_compute_shared_secret(bobIk.ikPriv, bobIk.ikPassword, aliceEkPub);
    std::string dh3 = dh_compute_shared_secret(bobSpk.priv, bobSpk.password, aliceEkPub);

    std::string combined = dh1 + dh2 + dh3;

    uint8_t rootKeyBytes[32];
    hkdf_sha256(combined, std::string("\x00", 1), "X3DH_v1", rootKeyBytes, 32);
    outRootKey = to_base64(rootKeyBytes, 32);

    LOG_INFO("[Signal] x3dh_respond_for_session: root key computed for session %s\n", sessionUuid.c_str());
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
    rec.sendN = 0;
    rec.recvN = 0;
    rec.prevRecvN = 0;
    rec.status = 0;

    if (isInitiator) {
        // Alice (initiator): generate new DH key pair, derive sending chain
        rec.dhPeerPub = peerDhPub;
        std::string dhPub, dhPriv, dhPassword;
        dhPassword = generate_random_password(32);
        if (!generate_ecc_keypair(dhPub, dhPriv, dhPassword)) {
            LOG_INFO("[Signal] dr_init: failed to generate DH keypair\n");
            return false;
        }
        rec.dhSelfPub = dhPub;
        rec.dhSelfPriv = dhPriv;
        rec.dhSelfPassword = dhPassword;

        // Derive initial sending chain: RK, CKs = KDF(RK, DH(DH_self, DH_peer))
        std::string dhOut = dh_compute_shared_secret(dhPriv, dhPassword, peerDhPub);
        std::string newRk, ck;
        kdf_rk_ck(dhOut, rootKey, newRk, ck);
        rec.rootKey = newRk;
        rec.sendChainKey = ck;
        rec.recvChainKey = "";  // No receiving chain yet
    } else {
        // Bob (responder): use signed prekey as initial DH key
        // This must match what Alice used (she derived her sending chain from DH(Alice_DR, Bob_SPK))
        // Leave dhPeerPub empty so dr_decrypt triggers DH ratchet and derives recv chain
        // from DH(Bob_SPK_priv, Alice_DR_pub) = DH(Alice_DR_priv, Bob_SPK_pub)
        SignalPrekeyRecord spkRec;
        if (!s_keyRepo.loadSignedPrekey(account, spkRec)) {
            LOG_INFO("[Signal] dr_init: no signed prekey for responder %s\n", account.c_str());
            return false;
        }
        rec.dhPeerPub = "";
        rec.dhSelfPub = spkRec.pub;
        rec.dhSelfPriv = spkRec.priv;
        rec.dhSelfPassword = spkRec.password;
        rec.sendChainKey = "";
        rec.recvChainKey = "";
    }

    return s_sessionRepo.saveSession(rec);
}

bool dr_initialize_for_session(const std::string& sessionUuid,
                                const std::string& account, const std::string& peerEmail,
                                const std::string& sessionId, const std::string& rootKey,
                                const std::string& peerDhPub, bool isInitiator) {
    SignalSessionRecord rec;
    rec.account = account;
    rec.peerEmail = peerEmail;
    rec.sessionId = sessionId;
    rec.rootKey = rootKey;
    rec.sendN = 0;
    rec.recvN = 0;
    rec.prevRecvN = 0;
    rec.status = 0;

    if (isInitiator) {
        rec.dhPeerPub = peerDhPub;
        std::string dhPub, dhPriv, dhPassword;
        dhPassword = generate_random_password(32);
        if (!generate_ecc_keypair(dhPub, dhPriv, dhPassword)) {
            LOG_INFO("[Signal] dr_init_for_session: failed to generate DH keypair\n");
            return false;
        }
        rec.dhSelfPub = dhPub;
        rec.dhSelfPriv = dhPriv;
        rec.dhSelfPassword = dhPassword;

        std::string dhOut = dh_compute_shared_secret(dhPriv, dhPassword, peerDhPub);
        std::string newRk, ck;
        kdf_rk_ck(dhOut, rootKey, newRk, ck);
        rec.rootKey = newRk;
        rec.sendChainKey = ck;
        rec.recvChainKey = "";
    } else {
        SignalPrekeyRecord spkRec;
        if (!s_keyRepo.loadSignedPrekeyForSession(sessionUuid, spkRec)) {
            LOG_INFO("[Signal] dr_init_for_session: no signed prekey for session %s\n", sessionUuid.c_str());
            return false;
        }
        rec.dhPeerPub = "";
        rec.dhSelfPub = spkRec.pub;
        rec.dhSelfPriv = spkRec.priv;
        rec.dhSelfPassword = spkRec.password;
        rec.sendChainKey = "";
        rec.recvChainKey = "";
    }

    return s_sessionRepo.saveSession(rec);
}

bool dr_encrypt(const std::string& account, const std::string& peerEmail,
                const std::string& sessionId, const std::string& plaintext,
                EncryptedMessage& outMsg) {
    SignalSessionRecord rec;
    if (!s_sessionRepo.loadSession(account, peerEmail, sessionId, rec)) {
        LOG_INFO("[Signal] dr_encrypt: session not found\n");
        return false;
    }

    if (rec.sendChainKey.empty()) {
        // Need to perform DH ratchet first (generate new DH pair)
        std::string dhPub, dhPriv, dhPassword;
        dhPassword = generate_random_password(32);
        if (!generate_ecc_keypair(dhPub, dhPriv, dhPassword)) {
            LOG_INFO("[Signal] dr_encrypt: failed to generate DH keypair\n");
            return false;
        }

        // Derive new root key and sending chain
        std::string dhOut = dh_compute_shared_secret(dhPriv, dhPassword, rec.dhPeerPub);
        std::string newRk, ck;
        kdf_rk_ck(dhOut, rec.rootKey, newRk, ck);

        rec.prevRecvN = rec.recvN;
        rec.rootKey = newRk;
        rec.sendChainKey = ck;
        rec.sendN = 0;
        rec.dhSelfPub = dhPub;
        rec.dhSelfPriv = dhPriv;
        rec.dhSelfPassword = dhPassword;
        rec.recvN = 0;
        rec.recvChainKey = "";
    }

    // Derive message key and advance chain
    std::string mk, newCk;
    kdf_mk(rec.sendChainKey, mk, newCk);

    // Encrypt plaintext with message key
    auto mkBytes = from_base64(mk);
    std::string mkStr(mkBytes.begin(), mkBytes.end());
    outMsg.ciphertext = aes_encrypt(mkStr, plaintext);
    outMsg.header.dhPub = rec.dhSelfPub;
    outMsg.header.n = rec.sendN;
    outMsg.header.pn = rec.prevRecvN;
    outMsg.header.msgType = "msg";

    // Update session state
    rec.sendChainKey = newCk;
    rec.sendN++;
    s_sessionRepo.saveSession(rec);

    LOG_INFO("[Signal] dr_encrypt: encrypted msg n=%d, pn=%d for session=%s\n",
             outMsg.header.n, outMsg.header.pn, sessionId.c_str());
    return true;
}

DecryptResult dr_decrypt(const std::string& account, const std::string& peerEmail,
                         const std::string& sessionId, const EncryptedMessage& msg) {
    DecryptResult result;
    result.sessionId = sessionId;

    SignalSessionRecord rec;
    if (!s_sessionRepo.loadSession(account, peerEmail, sessionId, rec)) {
        result.error = "session_not_found";
        return result;
    }

    // Check skipped keys first
    SignalSkippedKeyRecord skippedRec;
    if (s_sessionRepo.findSkippedKey(sessionId, msg.header.dhPub, msg.header.n, skippedRec)) {
        auto mkBytes = from_base64(skippedRec.messageKey);
        std::string mkStr(mkBytes.begin(), mkBytes.end());
        result.plaintext = aes_decrypt(mkStr, msg.ciphertext);
        s_sessionRepo.deleteSkippedKey(sessionId, msg.header.dhPub, msg.header.n);
        result.success = !result.plaintext.empty();
        if (!result.success) result.error = "decrypt_failed_skipped";
        return result;
    }

    // Check if DH ratchet is needed (new peer DH key)
    if (rec.dhPeerPub.empty() || msg.header.dhPub != rec.dhPeerPub) {
        // DH Ratchet step
        // 1. Save skipped keys for remaining messages in old receiving chain
        if (!rec.recvChainKey.empty() && !rec.dhPeerPub.empty()) {
            // Skip remaining messages up to pn (previous sending chain length from sender)
            // Actually, we skip from current recvN to the pn value
            // The sender's pn tells us how many messages were in their previous sending chain
            // We don't need to skip here; the skipped keys are handled per-message
        }

        // 2. Update peer DH
        rec.dhPeerPub = msg.header.dhPub;
        rec.recvN = 0;

        // 3. Generate new DH pair if we don't have one (responder first reply)
        if (rec.dhSelfPub.empty()) {
            std::string dhPub, dhPriv, dhPassword;
            dhPassword = generate_random_password(32);
            if (!generate_ecc_keypair(dhPub, dhPriv, dhPassword)) {
                result.error = "dh_keypair_failed";
                return result;
            }
            rec.dhSelfPub = dhPub;
            rec.dhSelfPriv = dhPriv;
            rec.dhSelfPassword = dhPassword;
        }

        // 4. Derive new root key and receiving chain
        std::string dhOut = dh_compute_shared_secret(rec.dhSelfPriv, rec.dhSelfPassword, msg.header.dhPub);
        std::string newRk, ck;
        kdf_rk_ck(dhOut, rec.rootKey, newRk, ck);
        rec.rootKey = newRk;
        rec.recvChainKey = ck;
        rec.recvN = 0;

        rec.sendChainKey.clear();
        rec.sendN = 0;
        rec.prevRecvN = 0;
    }

    // Skip messages if needed (handle out-of-order)
    while (rec.recvN < msg.header.n) {
        // Generate skipped message key
        std::string mk, newCk;
        kdf_mk(rec.recvChainKey, mk, newCk);
        
        // Store as skipped key
        if (s_sessionRepo.countSkippedKeys(sessionId) < 1000) {  // Limit
            s_sessionRepo.insertSkippedKey(sessionId, msg.header.dhPub, rec.recvN, mk);
        }
        
        rec.recvChainKey = newCk;
        rec.recvN++;
    }

    // Now decrypt the current message
    std::string mk, newCk;
    kdf_mk(rec.recvChainKey, mk, newCk);
    
    auto mkBytes = from_base64(mk);
    std::string mkStr(mkBytes.begin(), mkBytes.end());
    result.plaintext = aes_decrypt(mkStr, msg.ciphertext);

    rec.recvChainKey = newCk;
    rec.recvN++;
    s_sessionRepo.saveSession(rec);

    result.success = !result.plaintext.empty();
    if (!result.success) result.error = "decrypt_failed";
    
    LOG_INFO("[Signal] dr_decrypt: decrypted msg n=%d for session=%s, success=%d\n",
             msg.header.n, sessionId.c_str(), result.success);
    return result;
}
