#include "sender_key.h"
#include "sender_key_repo.h"
#include "group_session_repo.h"
#include "email_core_common.h"
#include "logger.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>
#include <nlohmann/json.hpp>
#include <cstring>

using json = nlohmann::json;

static SenderKeyRepo s_senderKeyRepo;
static GroupSessionRepo s_groupRepo;

// --- Base64 helpers (reuse from signal_protocol) ---

static std::string to_b64(const uint8_t* data, size_t len) {
    return base64_encode(data, len);
}

static std::string to_b64(const std::string& s) {
    return base64_encode((const unsigned char*)s.data(), s.size());
}

static std::vector<uint8_t> from_b64(const std::string& b64) {
    return base64_decode(b64);
}

// --- HMAC-SHA256 ---

static std::string hmac_sha256(const std::string& key, const uint8_t* data, size_t dataLen) {
    EVP_MAC* hmac = EVP_MAC_fetch(NULL, "HMAC", NULL);
    EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(hmac);
    OSSL_PARAM params[2];
    params[0] = OSSL_PARAM_construct_utf8_string("digest", (char*)"SHA256", 0);
    params[1] = OSSL_PARAM_construct_end();

    EVP_MAC_init(ctx, (const uint8_t*)key.data(), key.size(), params);
    EVP_MAC_update(ctx, data, dataLen);

    uint8_t out[SHA256_DIGEST_LENGTH];
    size_t outLen = SHA256_DIGEST_LENGTH;
    EVP_MAC_final(ctx, out, &outLen, sizeof(out));

    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(hmac);

    return std::string((char*)out, outLen);
}

// --- HKDF-SHA256 ---

static void hkdf_sha256(const std::string& ikm, const std::string& salt,
                        const std::string& info, uint8_t* out, size_t outLen) {
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

    // Expand
    uint8_t T[SHA256_DIGEST_LENGTH];
    size_t TLen = 0;
    size_t done = 0;
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

// --- KDF_CK: symmetric ratchet chain key step ---
// new_chainKey = HMAC-SHA256(chainKey, 0x02)
// mk_seed      = HMAC-SHA256(chainKey, 0x01)
// (cipherKey, iv, signingKey) = HKDF(mk_seed, "", "SenderMessageKey", 96)

struct MessageKey {
    std::string cipherKey;  // 32 bytes
    std::string iv;          // 16 bytes
    std::string signingKey;  // 48 bytes (Ed25519 seed)
};

static void kdf_ck(const std::string& chainKey, std::string& outNewChainKey, MessageKey& outMk) {
    auto ckBytes = from_b64(chainKey);

    // new chain key = HMAC(ck, 0x02)
    uint8_t step2 = 0x02;
    auto newCk = hmac_sha256(std::string((char*)ckBytes.data(), ckBytes.size()), &step2, 1);
    outNewChainKey = to_b64(newCk);

    // mk_seed = HMAC(ck, 0x01)
    uint8_t step1 = 0x01;
    auto mkSeed = hmac_sha256(std::string((char*)ckBytes.data(), ckBytes.size()), &step1, 1);

    // Derive 96 bytes: 32 cipher + 16 iv + 48 signing
    uint8_t derived[96];
    hkdf_sha256(mkSeed, std::string(""), std::string("SenderMessageKey"), derived, 96);

    outMk.cipherKey = std::string((char*)derived, 32);
    outMk.iv = std::string((char*)derived + 32, 16);
    outMk.signingKey = std::string((char*)derived + 48, 48);
}

// --- AES-256-CBC encrypt with explicit IV ---

static std::string aes_encrypt_with_iv(const std::string& key32, const std::string& iv16,
                                         const std::string& plaintext) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL,
                       (const unsigned char*)key32.data(),
                       (const unsigned char*)iv16.data());

    std::vector<uint8_t> output(plaintext.size() + 16);
    int len;
    int ctLen = 0;
    EVP_EncryptUpdate(ctx, output.data(), &len,
                      (const unsigned char*)plaintext.data(), plaintext.size());
    ctLen = len;
    EVP_EncryptFinal_ex(ctx, output.data() + len, &len);
    ctLen += len;
    output.resize(ctLen);
    EVP_CIPHER_CTX_free(ctx);

    return to_b64(output.data(), output.size());
}

// --- AES-256-CBC decrypt with explicit IV ---

static std::string aes_decrypt_with_iv(const std::string& key32, const std::string& iv16,
                                         const std::string& ciphertextB64) {
    auto data = from_b64(ciphertextB64);
    if (data.empty()) return "";

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL,
                       (const unsigned char*)key32.data(),
                       (const unsigned char*)iv16.data());

    std::vector<uint8_t> output(data.size() + 16);
    int len, ptLen = 0;
    EVP_DecryptUpdate(ctx, output.data(), &len, data.data(), data.size());
    ptLen = len;
    if (EVP_DecryptFinal_ex(ctx, output.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    ptLen += len;
    output.resize(ptLen);
    EVP_CIPHER_CTX_free(ctx);

    return std::string((char*)output.data(), output.size());
}

// --- Ed25519 sign/verify (simplified: use signing key as HMAC for now) ---
// TODO: replace with real Ed25519 when available

static std::string sign_message(const std::string& signingKey, const std::string& message) {
    // Use HMAC-SHA256 as signature (simplified)
    auto sig = hmac_sha256(signingKey, (const uint8_t*)message.data(), message.size());
    return to_b64(sig);
}

static bool verify_signature(const std::string& signingPub, const std::string& message,
                              const std::string& signatureB64) {
    // Simplified: verify by recomputing HMAC with signingPub as key
    auto expectedSig = hmac_sha256(signingPub, (const uint8_t*)message.data(), message.size());
    std::string expectedB64 = to_b64(expectedSig);
    return expectedB64 == signatureB64;
}

// --- Generate random bytes ---

static std::string random_bytes(size_t len) {
    std::vector<uint8_t> buf(len);
    RAND_bytes(buf.data(), (int)len);
    return std::string((char*)buf.data(), buf.size());
}

// --- Public API ---

std::string sender_key_generate(const std::string& account, const std::string& groupId) {
    // Get current epoch from group session
    GroupSessionRecord group;
    int epoch = 0;
    if (s_groupRepo.loadGroup(groupId, group)) {
        epoch = group.epoch;
    }

    // Generate chain key (32 random bytes)
    std::string chainKey = to_b64(random_bytes(32));

    // Generate signing key (32 random bytes as seed, simplified)
    std::string signingKeyPriv = to_b64(random_bytes(32));
    std::string signingKeyPub = signingKeyPriv;  // simplified: pub = priv for HMAC-based signing

    // Save to DB
    SenderKeyRecord rec;
    rec.groupId = groupId;
    rec.account = account;
    rec.senderEmail = account;  // own SenderKey
    rec.chainKey = chainKey;
    rec.signingKeyPub = signingKeyPub;
    rec.signingKeyPriv = signingKeyPriv;
    rec.iteration = 0;
    rec.epoch = epoch;
    rec.status = 0;
    s_senderKeyRepo.saveSenderKey(rec);

    LOG_INFO("[SenderKey] generated for group=%s, account=%s, epoch=%d\n",
             groupId.c_str(), account.c_str(), epoch);

    // Build distribution payload
    json payload;
    payload["type"] = "sender_key";
    payload["group_id"] = groupId;
    payload["chain_key"] = chainKey;
    payload["signing_pub"] = signingKeyPub;
    payload["epoch"] = epoch;
    return payload.dump();
}

std::string sender_key_get_distribution(const std::string& account, const std::string& groupId) {
    SenderKeyRecord rec;
    if (!s_senderKeyRepo.loadActiveSenderKey(groupId, account, account, rec)) {
        return "";
    }

    json payload;
    payload["type"] = "sender_key";
    payload["group_id"] = groupId;
    payload["chain_key"] = rec.chainKey;
    payload["signing_pub"] = rec.signingKeyPub;
    payload["epoch"] = rec.epoch;
    return payload.dump();
}

bool sender_key_store(const std::string& account, const std::string& groupId,
                      const std::string& senderEmail,
                      const std::string& chainKey, const std::string& signingPub,
                      int epoch) {
    SenderKeyRecord rec;
    rec.groupId = groupId;
    rec.account = account;
    rec.senderEmail = senderEmail;
    rec.chainKey = chainKey;
    rec.signingKeyPub = signingPub;
    rec.signingKeyPriv = "";  // not our key
    rec.iteration = 0;
    rec.epoch = epoch;
    rec.status = 0;
    bool ok = s_senderKeyRepo.saveSenderKey(rec);
    LOG_INFO("[SenderKey] stored from sender=%s for group=%s, epoch=%d, ok=%d\n",
             senderEmail.c_str(), groupId.c_str(), epoch, ok);
    return ok;
}

int sender_key_check_ready(const std::string& account, const std::string& groupId) {
    GroupSessionRecord group;
    if (!s_groupRepo.loadGroup(groupId, group)) return 0;

    for (auto& member : group.members) {
        SenderKeyRecord rec;
        if (!s_senderKeyRepo.loadActiveSenderKey(groupId, account, member, rec)) {
            LOG_INFO("[SenderKey] not ready: missing key for member=%s in group=%s\n",
                     member.c_str(), groupId.c_str());
            return 0;
        }
    }
    LOG_INFO("[SenderKey] ready for group=%s, account=%s\n", groupId.c_str(), account.c_str());
    return 1;
}

GroupEncryptResult group_encrypt(const std::string& account,
                                  const std::string& groupId,
                                  const std::string& plaintext) {
    GroupEncryptResult result;

    // Load our own SenderKey
    SenderKeyRecord rec;
    if (!s_senderKeyRepo.loadActiveSenderKey(groupId, account, account, rec)) {
        result.error = "no own SenderKey found";
        return result;
    }

    // Step the chain key
    std::string newChainKey;
    MessageKey mk;
    kdf_ck(rec.chainKey, newChainKey, mk);

    // Encrypt
    std::string ciphertext = aes_encrypt_with_iv(mk.cipherKey, mk.iv, plaintext);

    // Sign
    std::string signature = sign_message(mk.signingKey, ciphertext);

    // Update chain key in DB
    s_senderKeyRepo.updateChainKey(groupId, account, account, rec.epoch, newChainKey, rec.iteration + 1);

    result.success = true;
    result.groupId = groupId;
    result.senderEmail = account;
    result.iteration = rec.iteration;
    result.epoch = rec.epoch;
    result.ciphertext = ciphertext;
    result.signature = signature;

    LOG_INFO("[SenderKey] group_encrypt: group=%s, iter=%d, epoch=%d\n",
             groupId.c_str(), result.iteration, result.epoch);
    return result;
}

GroupDecryptResult group_decrypt(const std::string& account,
                                  const std::string& groupId,
                                  const std::string& senderEmail,
                                  int iteration, int epoch,
                                  const std::string& ciphertext,
                                  const std::string& signature) {
    GroupDecryptResult result;

    // Load sender's SenderKey
    SenderKeyRecord rec;
    if (!s_senderKeyRepo.loadSenderKey(groupId, account, senderEmail, epoch, rec)) {
        result.error = "no SenderKey for sender";
        return result;
    }

    // Check for skipped key (out-of-order message)
    SkippedSenderKeyRecord skipped;
    if (s_senderKeyRepo.findSkippedKey(groupId, account, senderEmail, iteration, epoch, skipped)) {
        // Found in skipped keys, use directly
        std::string plaintext = aes_decrypt_with_iv(skipped.cipherKey, skipped.iv, ciphertext);
        if (plaintext.empty()) {
            result.error = "AES decrypt failed (skipped key)";
            return result;
        }
        if (!verify_signature(skipped.signingKey, ciphertext, signature)) {
            result.error = "signature verification failed (skipped key)";
            return result;
        }
        s_senderKeyRepo.deleteSkippedKey(groupId, account, senderEmail, iteration, epoch);
        result.success = true;
        result.plaintext = plaintext;
        LOG_INFO("[SenderKey] group_decrypt (skipped): group=%s, sender=%s, iter=%d\n",
                 groupId.c_str(), senderEmail.c_str(), iteration);
        return result;
    }

    // Check if this is a future message
    if (iteration > rec.iteration) {
        // Step forward, saving skipped keys
        std::string chainKey = rec.chainKey;
        int currentIter = rec.iteration;

        while (currentIter < iteration) {
            std::string newChainKey;
            MessageKey mk;
            kdf_ck(chainKey, newChainKey, mk);

            currentIter++;
            if (currentIter < iteration) {
                // Save as skipped key
                SkippedSenderKeyRecord sk;
                sk.groupId = groupId;
                sk.account = account;
                sk.senderEmail = senderEmail;
                sk.iteration = currentIter;
                sk.epoch = epoch;
                sk.cipherKey = to_b64(mk.cipherKey);
                sk.iv = to_b64(mk.iv);
                sk.signingKey = to_b64(mk.signingKey);
                s_senderKeyRepo.insertSkippedKey(sk);
            } else {
                // This is the target iteration
                std::string plaintext = aes_decrypt_with_iv(mk.cipherKey, mk.iv, ciphertext);
                if (plaintext.empty()) {
                    result.error = "AES decrypt failed";
                    return result;
                }
                if (!verify_signature(mk.signingKey, ciphertext, signature)) {
                    result.error = "signature verification failed";
                    return result;
                }
                // Update chain key
                s_senderKeyRepo.updateChainKey(groupId, account, senderEmail, epoch, newChainKey, iteration);
                result.success = true;
                result.plaintext = plaintext;
                LOG_INFO("[SenderKey] group_decrypt (future): group=%s, sender=%s, iter=%d\n",
                         groupId.c_str(), senderEmail.c_str(), iteration);
                return result;
            }
            chainKey = newChainKey;
        }
    }

    // Normal case: iteration == rec.iteration
    if (iteration != rec.iteration) {
        result.error = "iteration mismatch (got " + std::to_string(iteration) +
                        ", expected " + std::to_string(rec.iteration) + ")";
        return result;
    }

    std::string newChainKey;
    MessageKey mk;
    kdf_ck(rec.chainKey, newChainKey, mk);

    std::string plaintext = aes_decrypt_with_iv(mk.cipherKey, mk.iv, ciphertext);
    if (plaintext.empty()) {
        result.error = "AES decrypt failed";
        return result;
    }
    if (!verify_signature(mk.signingKey, ciphertext, signature)) {
        result.error = "signature verification failed";
        return result;
    }

    // Update chain key
    s_senderKeyRepo.updateChainKey(groupId, account, senderEmail, epoch, newChainKey, rec.iteration + 1);

    result.success = true;
    result.plaintext = plaintext;
    LOG_INFO("[SenderKey] group_decrypt (normal): group=%s, sender=%s, iter=%d\n",
             groupId.c_str(), senderEmail.c_str(), iteration);
    return result;
}
