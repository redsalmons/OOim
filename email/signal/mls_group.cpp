#include "mls_group.h"
#include "mls_ffi.h"
#include "mls_repo.h"
#include "email_core_common.h"
#include "logger.h"
#include <nlohmann/json.hpp>
#include <mutex>
#include <set>

using json = nlohmann::json;

namespace {

std::mutex g_mutex;
std::set<std::string> g_loaded;   // accounts whose state is live in the Rust runtime
MlsRepo g_repo;

// Call a (out,len)-style FFI function, growing the buffer on -(needed).
template <typename F>
int callOut(F fn, std::vector<uint8_t>& out) {
    out.resize(64 * 1024);
    int rc = fn(out.data(), (int)out.size());
    if (rc < -4) {  // -(needed): buffer too small
        out.resize((size_t)(-rc) + 1);
        rc = fn(out.data(), (int)out.size());
    }
    if (rc >= 0) out.resize((size_t)rc);
    return rc;
}

bool persist(const std::string& account) {
    std::vector<uint8_t> blob;
    int rc = callOut([&](uint8_t* o, int n) { return mls_save_state(account.c_str(), o, n); }, blob);
    if (rc < 0) { LOG_INFO("[MLS] save_state failed account=%s rc=%d\n", account.c_str(), rc); return false; }
    return g_repo.saveState(account, blob);
}

bool ensureLocked(const std::string& account) {
    if (g_loaded.count(account)) return true;
    std::vector<uint8_t> blob;
    if (g_repo.loadState(account, blob)) {
        int rc = mls_load_state(account.c_str(), blob.data(), (int)blob.size());
        if (rc != 0) { LOG_INFO("[MLS] load_state failed account=%s rc=%d\n", account.c_str(), rc); return false; }
        LOG_INFO("[MLS] restored state for %s (%zu bytes)\n", account.c_str(), blob.size());
    } else {
        int rc = mls_init(account.c_str());
        if (rc != 0) { LOG_INFO("[MLS] init failed account=%s rc=%d\n", account.c_str(), rc); return false; }
        LOG_INFO("[MLS] created new identity for %s\n", account.c_str());
        g_loaded.insert(account);
        persist(account);
        return true;
    }
    g_loaded.insert(account);
    return true;
}

std::string mlsGid(const std::string& groupId) { return g_repo.getMlsGroupId(groupId); }

std::string b64(const std::vector<uint8_t>& v) { return base64_encode(v.data(), v.size()); }

} // namespace

namespace mls {

bool ensureAccount(const std::string& account) {
    std::lock_guard<std::mutex> lk(g_mutex);
    return ensureLocked(account);
}

std::string generateKeyPackage(const std::string& account) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!ensureLocked(account)) return "";
    std::vector<uint8_t> kp;
    int rc = callOut([&](uint8_t* o, int n) { return mls_generate_key_package(account.c_str(), o, n); }, kp);
    if (rc < 0) { LOG_INFO("[MLS] generate_key_package failed rc=%d\n", rc); return ""; }
    persist(account);  // the KeyPackage's private init key lives in the state
    return b64(kp);
}

std::string createGroup(const std::string& account, const std::string& groupId) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!ensureLocked(account)) return "";
    std::vector<uint8_t> gid;
    int rc = callOut([&](uint8_t* o, int n) { return mls_create_group(account.c_str(), o, n); }, gid);
    if (rc < 0) { LOG_INFO("[MLS] create_group failed rc=%d\n", rc); return ""; }
    std::string hex(gid.begin(), gid.end());
    g_repo.setMlsGroupId(groupId, hex);
    persist(account);
    LOG_INFO("[MLS] created group local=%s mls=%s account=%s\n", groupId.c_str(), hex.c_str(), account.c_str());
    return hex;
}

AddResult addMembers(const std::string& account, const std::string& groupId,
                     const std::vector<std::string>& keyPackagesB64) {
    AddResult r;
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!ensureLocked(account)) { r.error = "account init failed"; return r; }
    std::string gid = mlsGid(groupId);
    if (gid.empty()) { r.error = "group has no MLS id"; return r; }

    std::string kpsJson = json(keyPackagesB64).dump();
    std::vector<uint8_t> welcome(256 * 1024), commit(256 * 1024);
    int wl = 0, cl = 0;
    int rc = mls_add_members(account.c_str(), gid.c_str(), kpsJson.c_str(),
                             welcome.data(), (int)welcome.size(), &wl,
                             commit.data(), (int)commit.size(), &cl);
    if (rc != 0) { r.error = "mls_add_members rc=" + std::to_string(rc); LOG_INFO("[MLS] %s\n", r.error.c_str()); return r; }
    welcome.resize(wl); commit.resize(cl);

    std::vector<uint8_t> tree;
    rc = callOut([&](uint8_t* o, int n) { return mls_export_ratchet_tree(account.c_str(), gid.c_str(), o, n); }, tree);
    if (rc < 0) { r.error = "export_ratchet_tree rc=" + std::to_string(rc); return r; }

    persist(account);
    r.ok = true;
    r.welcomeB64 = b64(welcome);
    r.commitB64 = b64(commit);
    r.treeB64 = b64(tree);
    LOG_INFO("[MLS] add_members group=%s n=%zu epoch=%d\n", groupId.c_str(), keyPackagesB64.size(),
             mls_get_epoch(account.c_str(), gid.c_str()));
    return r;
}

std::string joinGroup(const std::string& account, const std::string& groupId,
                      const std::string& welcomeB64, const std::string& treeB64) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!ensureLocked(account)) return "";
    auto welcome = base64_decode(welcomeB64);
    auto tree = base64_decode(treeB64);
    std::vector<uint8_t> gid;
    int rc = callOut([&](uint8_t* o, int n) {
        return mls_join_group(account.c_str(), welcome.data(), (int)welcome.size(),
                              tree.data(), (int)tree.size(), o, n);
    }, gid);
    if (rc < 0) { LOG_INFO("[MLS] join_group failed rc=%d group=%s\n", rc, groupId.c_str()); return ""; }
    std::string hex(gid.begin(), gid.end());
    g_repo.setMlsGroupId(groupId, hex);
    persist(account);
    LOG_INFO("[MLS] joined group local=%s mls=%s account=%s epoch=%d\n", groupId.c_str(), hex.c_str(),
             account.c_str(), mls_get_epoch(account.c_str(), hex.c_str()));
    return hex;
}

bool encrypt(const std::string& account, const std::string& groupId, const std::string& plaintext, std::string& outCipherB64) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!ensureLocked(account)) return false;
    std::string gid = mlsGid(groupId);
    if (gid.empty()) return false;
    // Single call: mls_encrypt consumes a ratchet generation even when the output
    // buffer is too small, so callOut's retry must NOT be used here. MLS framing
    // adds only a few KB, so plaintext+64K is a safe upper bound.
    std::vector<uint8_t> ct(plaintext.size() + 64 * 1024);
    int rc = mls_encrypt(account.c_str(), gid.c_str(),
                         (const uint8_t*)plaintext.data(), (int)plaintext.size(),
                         ct.data(), (int)ct.size());
    if (rc < 0) {
        std::vector<uint8_t> eb(4096);
        int el = mls_last_error(eb.data(), (int)eb.size());
        LOG_INFO("[MLS] encrypt failed rc=%d group=%s err=%.*s\n", rc, groupId.c_str(), el, eb.data());
        return false;
    }
    ct.resize((size_t)rc);
    persist(account);
    outCipherB64 = b64(ct);
    return true;
}

bool decrypt(const std::string& account, const std::string& groupId, const std::string& cipherB64, std::string& outPlaintext) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!ensureLocked(account)) return false;
    std::string gid = mlsGid(groupId);
    if (gid.empty()) return false;
    auto ct = base64_decode(cipherB64);
    // Single call for the same reason: mls_decrypt consumes the message key on the
    // first attempt — a buffer-too-small retry hits SecretReuseError. Plaintext is
    // always smaller than the AEAD ciphertext, so ct_len+64K always fits.
    std::vector<uint8_t> pt(ct.size() + 64 * 1024);
    int rc = mls_decrypt(account.c_str(), gid.c_str(), ct.data(), (int)ct.size(),
                         pt.data(), (int)pt.size());
    if (rc < 0) {
        std::vector<uint8_t> eb(4096);
        int el = mls_last_error(eb.data(), (int)eb.size());
        LOG_INFO("[MLS] decrypt failed rc=%d group=%s err=%.*s\n", rc, groupId.c_str(), el, eb.data());
        return false;
    }
    pt.resize((size_t)rc);
    persist(account);
    outPlaintext.assign(pt.begin(), pt.end());
    return true;
}

bool processCommit(const std::string& account, const std::string& groupId, const std::string& commitB64) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!ensureLocked(account)) return false;
    std::string gid = mlsGid(groupId);
    if (gid.empty()) return false;
    auto c = base64_decode(commitB64);
    int rc = mls_process_commit(account.c_str(), gid.c_str(), c.data(), (int)c.size());
    if (rc != 0) { LOG_INFO("[MLS] process_commit failed rc=%d group=%s\n", rc, groupId.c_str()); return false; }
    persist(account);
    LOG_INFO("[MLS] commit applied group=%s epoch=%d\n", groupId.c_str(), mls_get_epoch(account.c_str(), gid.c_str()));
    return true;
}

std::vector<std::string> members(const std::string& account, const std::string& groupId) {
    std::vector<std::string> out;
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!ensureLocked(account)) return out;
    std::string gid = mlsGid(groupId);
    if (gid.empty()) return out;
    std::vector<uint8_t> buf;
    int rc = callOut([&](uint8_t* o, int n) { return mls_get_members(account.c_str(), gid.c_str(), o, n); }, buf);
    if (rc < 0) return out;
    try {
        for (auto& m : json::parse(std::string(buf.begin(), buf.end()))) out.push_back(m.value("identity", ""));
    } catch (...) {}
    return out;
}

bool isReady(const std::string& account, const std::string& groupId) {
    return epoch(account, groupId) >= 0;
}

int epoch(const std::string& account, const std::string& groupId) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!ensureLocked(account)) return -1;
    std::string gid = mlsGid(groupId);
    if (gid.empty()) return -1;
    return mls_get_epoch(account.c_str(), gid.c_str());
}

} // namespace mls
