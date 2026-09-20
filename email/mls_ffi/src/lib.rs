// MLS (RFC 9420) FFI wrapper for OIM, built on OpenMLS 0.8.
//
// Group (1:n) protocol only. Completely independent from the 1:1 Double Ratchet code.
//
// Conventions for the C ABI:
//   * `account`       : NUL-terminated UTF-8 email address of the local account
//   * `group_id_hex`  : NUL-terminated hex string of the MLS GroupId
//   * binary in/out   : (ptr, len) pairs; output functions return the number of bytes
//                       written (>0), or a negative error code. If the output buffer is
//                       too small the return value is -(needed_size) and nothing is written.
//   * All state for an account lives in memory and must be persisted by the caller via
//     `mls_save_state` / `mls_load_state` (opaque blob).

use std::collections::HashMap;
use std::ffi::CStr;
use std::os::raw::{c_char, c_int};
use std::sync::Mutex;

use base64::Engine;
use openmls::prelude::tls_codec::{Deserialize as TlsDeserialize, Serialize as TlsSerialize};
use openmls::prelude::*;
use openmls_basic_credential::SignatureKeyPair;
use openmls_rust_crypto::{MemoryStorage, RustCrypto};
use openmls_traits::OpenMlsProvider;

const CIPHERSUITE: Ciphersuite = Ciphersuite::MLS_128_DHKEMX25519_AES128GCM_SHA256_Ed25519;

// Error codes shared by every entry point
const ERR_BAD_ARG: c_int = -1;
const ERR_NO_ACCOUNT: c_int = -2;
const ERR_NO_GROUP: c_int = -3;
const ERR_MLS: c_int = -4;

// ─── Provider ────────────────────────────────────────────────────────────────

#[derive(Default)]
struct OimProvider {
    crypto: RustCrypto,
    storage: MemoryStorage,
}

impl OpenMlsProvider for OimProvider {
    type CryptoProvider = RustCrypto;
    type RandProvider = RustCrypto;
    type StorageProvider = MemoryStorage;

    fn storage(&self) -> &MemoryStorage {
        &self.storage
    }
    fn crypto(&self) -> &RustCrypto {
        &self.crypto
    }
    fn rand(&self) -> &RustCrypto {
        &self.crypto
    }
}

struct AccountState {
    provider: OimProvider,
    signer: SignatureKeyPair,
    credential: CredentialWithKey,
    groups: HashMap<String, MlsGroup>,
}

impl AccountState {
    fn new(account: &str) -> Result<Self, String> {
        let provider = OimProvider::default();
        let signer = SignatureKeyPair::new(CIPHERSUITE.signature_algorithm())
            .map_err(|e| format!("signature keypair: {e:?}"))?;
        signer
            .store(provider.storage())
            .map_err(|e| format!("store signer: {e:?}"))?;
        let credential = CredentialWithKey {
            credential: BasicCredential::new(account.as_bytes().to_vec()).into(),
            signature_key: signer.public().into(),
        };
        Ok(Self { provider, signer, credential, groups: HashMap::new() })
    }

    /// Split borrow: (provider, signer, group). The group is lazily loaded from storage.
    fn parts(&mut self, gid_hex: &str) -> Result<(&OimProvider, &SignatureKeyPair, &mut MlsGroup), String> {
        let AccountState { provider, signer, groups, .. } = self;
        if !groups.contains_key(gid_hex) {
            let gid = GroupId::from_slice(&hex_decode(gid_hex)?);
            let g = MlsGroup::load(provider.storage(), &gid)
                .map_err(|e| format!("load group: {e:?}"))?
                .ok_or_else(|| format!("group {gid_hex} not found"))?;
            groups.insert(gid_hex.to_string(), g);
        }
        Ok((provider, signer, groups.get_mut(gid_hex).unwrap()))
    }

    fn group(&mut self, gid_hex: &str) -> Result<&mut MlsGroup, String> {
        self.parts(gid_hex).map(|(_, _, g)| g)
    }
}

static ACCOUNTS: Mutex<Option<HashMap<String, AccountState>>> = Mutex::new(None);

fn with_account<R>(
    account: &str,
    create: bool,
    f: impl FnOnce(&mut AccountState) -> Result<R, String>,
) -> Result<R, (c_int, String)> {
    let mut guard = ACCOUNTS.lock().unwrap();
    let map = guard.get_or_insert_with(HashMap::new);
    if !map.contains_key(account) {
        if !create {
            return Err((ERR_NO_ACCOUNT, format!("account {account} not initialised")));
        }
        map.insert(account.to_string(), AccountState::new(account).map_err(|e| (ERR_MLS, e))?);
    }
    f(map.get_mut(account).unwrap()).map_err(|e| {
        let code = if e.contains("not found") { ERR_NO_GROUP } else { ERR_MLS };
        (code, e)
    })
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

fn cstr(p: *const c_char) -> Result<String, c_int> {
    if p.is_null() {
        return Err(ERR_BAD_ARG);
    }
    unsafe { CStr::from_ptr(p) }.to_str().map(str::to_owned).map_err(|_| ERR_BAD_ARG)
}

fn slice<'a>(p: *const u8, len: c_int) -> Result<&'a [u8], c_int> {
    if p.is_null() || len <= 0 {
        return Err(ERR_BAD_ARG);
    }
    Ok(unsafe { std::slice::from_raw_parts(p, len as usize) })
}

fn hex_encode(d: &[u8]) -> String {
    d.iter().map(|b| format!("{b:02x}")).collect()
}

fn hex_decode(s: &str) -> Result<Vec<u8>, String> {
    if s.len() % 2 != 0 {
        return Err("odd hex length".into());
    }
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).map_err(|e| e.to_string()))
        .collect()
}

/// Copy `data` into (out, out_len). Returns bytes written, or -(needed) if too small.
fn write_out(data: &[u8], out: *mut u8, out_len: c_int) -> c_int {
    if out.is_null() || (out_len as usize) < data.len() {
        return -(data.len() as c_int);
    }
    unsafe { std::ptr::copy_nonoverlapping(data.as_ptr(), out, data.len()) };
    data.len() as c_int
}

static LAST_ERROR: Mutex<Option<String>> = Mutex::new(None);

/// Copy the last FFI error string into `out`. Returns its length, 0 if none.
#[no_mangle]
pub extern "C" fn mls_last_error(out: *mut u8, out_len: c_int) -> c_int {
    let msg = LAST_ERROR.lock().unwrap().take().unwrap_or_default();
    write_out(msg.as_bytes(), out, out_len)
}

fn report(ctx: &str, r: Result<c_int, (c_int, String)>) -> c_int {
    match r {
        Ok(v) => v,
        Err((code, msg)) => {
            eprintln!("[mls_ffi] {ctx}: {msg}");
            *LAST_ERROR.lock().unwrap() = Some(format!("{ctx}: {msg}"));
            code
        }
    }
}

fn parse_key_package(bytes: &[u8], crypto: &RustCrypto) -> Result<KeyPackage, String> {
    let kp_in = KeyPackageIn::tls_deserialize_exact(bytes)
        .map_err(|e| format!("keypackage decode: {e:?}"))?;
    kp_in
        .validate(crypto, ProtocolVersion::Mls10)
        .map_err(|e| format!("keypackage validate: {e:?}"))
}

fn protocol_message(bytes: &[u8]) -> Result<ProtocolMessage, String> {
    MlsMessageIn::tls_deserialize_exact(bytes)
        .map_err(|e| format!("message decode: {e:?}"))?
        .try_into_protocol_message()
        .map_err(|e| format!("not a protocol message: {e:?}"))
}

// ─── C ABI ───────────────────────────────────────────────────────────────────

/// Initialise (or no-op if already initialised) the MLS identity for `account`.
#[no_mangle]
pub extern "C" fn mls_init(account: *const c_char) -> c_int {
    let Ok(account) = cstr(account) else { return ERR_BAD_ARG };
    report("mls_init", with_account(&account, true, |_| Ok(0)))
}

/// Generate a fresh KeyPackage (TLS-serialised) for `account`.
#[no_mangle]
pub extern "C" fn mls_generate_key_package(account: *const c_char, out: *mut u8, out_len: c_int) -> c_int {
    let Ok(account) = cstr(account) else { return ERR_BAD_ARG };
    report(
        "mls_generate_key_package",
        with_account(&account, true, |st| {
            let bundle = KeyPackage::builder()
                .build(CIPHERSUITE, &st.provider, &st.signer, st.credential.clone())
                .map_err(|e| format!("build keypackage: {e:?}"))?;
            let bytes = bundle
                .key_package()
                .tls_serialize_detached()
                .map_err(|e| format!("serialise keypackage: {e:?}"))?;
            Ok(write_out(&bytes, out, out_len))
        }),
    )
}

/// Create a group with `account` as the only member. Writes the hex group id to `out_gid`.
#[no_mangle]
pub extern "C" fn mls_create_group(account: *const c_char, out_gid: *mut u8, gid_len: c_int) -> c_int {
    let Ok(account) = cstr(account) else { return ERR_BAD_ARG };
    report(
        "mls_create_group",
        with_account(&account, true, |st| {
            let group = MlsGroup::new(&st.provider, &st.signer, &MlsGroupCreateConfig::default(), st.credential.clone())
                .map_err(|e| format!("create group: {e:?}"))?;
            let gid_hex = hex_encode(group.group_id().as_slice());
            st.groups.insert(gid_hex.clone(), group);
            Ok(write_out(gid_hex.as_bytes(), out_gid, gid_len))
        }),
    )
}

/// Add members (JSON array of base64 TLS KeyPackages) to a group.
/// Produces a Welcome (for the new members) and a Commit (for existing members).
/// Both outputs are TLS-serialised MlsMessageOut. Returns 0 on success.
#[no_mangle]
pub extern "C" fn mls_add_members(
    account: *const c_char,
    group_id_hex: *const c_char,
    key_packages_json: *const c_char,
    out_welcome: *mut u8,
    welcome_len: c_int,
    out_welcome_written: *mut c_int,
    out_commit: *mut u8,
    commit_len: c_int,
    out_commit_written: *mut c_int,
) -> c_int {
    let (Ok(account), Ok(gid), Ok(kps_json)) = (cstr(account), cstr(group_id_hex), cstr(key_packages_json)) else {
        return ERR_BAD_ARG;
    };
    report(
        "mls_add_members",
        with_account(&account, false, |st| {
            let kps_b64: Vec<String> = serde_json::from_str(&kps_json).map_err(|e| format!("json: {e}"))?;
            let mut kps = Vec::with_capacity(kps_b64.len());
            for b64 in &kps_b64 {
                let bytes = base64::engine::general_purpose::STANDARD.decode(b64).map_err(|e| format!("base64: {e}"))?;
                kps.push(parse_key_package(&bytes, &st.provider.crypto)?);
            }
            let (provider, signer, group) = st.parts(&gid)?;
            let (commit, welcome, _info) = group.add_members(provider, signer, &kps).map_err(|e| format!("add_members: {e:?}"))?;
            group.merge_pending_commit(provider).map_err(|e| format!("merge commit: {e:?}"))?;
            let w = welcome.tls_serialize_detached().map_err(|e| format!("serialise welcome: {e:?}"))?;
            let c = commit.tls_serialize_detached().map_err(|e| format!("serialise commit: {e:?}"))?;
            let wr = write_out(&w, out_welcome, welcome_len);
            let cr = write_out(&c, out_commit, commit_len);
            if wr < 0 || cr < 0 {
                return Err(format!("buffer too small (welcome {}, commit {})", w.len(), c.len()));
            }
            unsafe {
                if !out_welcome_written.is_null() { *out_welcome_written = wr; }
                if !out_commit_written.is_null() { *out_commit_written = cr; }
            }
            Ok(0)
        }),
    )
}

/// Join a group from a Welcome. `ratchet_tree` is the TLS-serialised RatchetTree exported
/// by the inviter (required, since we have no delivery service). Writes hex group id.
#[no_mangle]
pub extern "C" fn mls_join_group(
    account: *const c_char,
    welcome: *const u8,
    welcome_len: c_int,
    ratchet_tree: *const u8,
    tree_len: c_int,
    out_gid: *mut u8,
    gid_len: c_int,
) -> c_int {
    let (Ok(account), Ok(welcome), Ok(tree)) = (cstr(account), slice(welcome, welcome_len), slice(ratchet_tree, tree_len)) else {
        return ERR_BAD_ARG;
    };
    report(
        "mls_join_group",
        with_account(&account, true, |st| {
            let msg = MlsMessageIn::tls_deserialize_exact(welcome).map_err(|e| format!("welcome decode: {e:?}"))?;
            let MlsMessageBodyIn::Welcome(w) = msg.extract() else { return Err("not a Welcome".into()) };
            let tree = RatchetTreeIn::tls_deserialize_exact(tree).map_err(|e| format!("tree decode: {e:?}"))?;
            let group = StagedWelcome::new_from_welcome(&st.provider, &MlsGroupJoinConfig::default(), w, Some(tree))
                .map_err(|e| format!("staged welcome: {e:?}"))?
                .into_group(&st.provider)
                .map_err(|e| format!("into_group: {e:?}"))?;
            let gid_hex = hex_encode(group.group_id().as_slice());
            st.groups.insert(gid_hex.clone(), group);
            Ok(write_out(gid_hex.as_bytes(), out_gid, gid_len))
        }),
    )
}

/// Export the TLS-serialised RatchetTree of a group (sent alongside a Welcome).
#[no_mangle]
pub extern "C" fn mls_export_ratchet_tree(account: *const c_char, group_id_hex: *const c_char, out: *mut u8, out_len: c_int) -> c_int {
    let (Ok(account), Ok(gid)) = (cstr(account), cstr(group_id_hex)) else { return ERR_BAD_ARG };
    report(
        "mls_export_ratchet_tree",
        with_account(&account, false, |st| {
            let bytes = st.group(&gid)?.export_ratchet_tree().tls_serialize_detached().map_err(|e| format!("serialise tree: {e:?}"))?;
            Ok(write_out(&bytes, out, out_len))
        }),
    )
}

/// Encrypt an application message. Output is a TLS-serialised MlsMessageOut.
#[no_mangle]
pub extern "C" fn mls_encrypt(
    account: *const c_char,
    group_id_hex: *const c_char,
    plaintext: *const u8,
    pt_len: c_int,
    out: *mut u8,
    out_len: c_int,
) -> c_int {
    let (Ok(account), Ok(gid), Ok(pt)) = (cstr(account), cstr(group_id_hex), slice(plaintext, pt_len)) else { return ERR_BAD_ARG };
    report(
        "mls_encrypt",
        with_account(&account, false, |st| {
            let (provider, signer, group) = st.parts(&gid)?;
            let msg = group.create_message(provider, signer, pt).map_err(|e| format!("create_message: {e:?}"))?;
            let bytes = msg.tls_serialize_detached().map_err(|e| format!("serialise: {e:?}"))?;
            Ok(write_out(&bytes, out, out_len))
        }),
    )
}

/// Decrypt an application message. Returns plaintext length, or ERR_MLS for non-app messages.
#[no_mangle]
pub extern "C" fn mls_decrypt(
    account: *const c_char,
    group_id_hex: *const c_char,
    ciphertext: *const u8,
    ct_len: c_int,
    out: *mut u8,
    out_len: c_int,
) -> c_int {
    let (Ok(account), Ok(gid), Ok(ct)) = (cstr(account), cstr(group_id_hex), slice(ciphertext, ct_len)) else { return ERR_BAD_ARG };
    report(
        "mls_decrypt",
        with_account(&account, false, |st| {
            let pm = protocol_message(ct)?;
            let (provider, _, group) = st.parts(&gid)?;
            let processed = group.process_message(provider, pm).map_err(|e| format!("process_message: {e:?}"))?;
            match processed.into_content() {
                ProcessedMessageContent::ApplicationMessage(m) => Ok(write_out(&m.into_bytes(), out, out_len)),
                other => Err(format!("not an application message: {:?}", std::mem::discriminant(&other))),
            }
        }),
    )
}

/// Process a Commit from another member and merge it (advances the epoch). Returns 0.
#[no_mangle]
pub extern "C" fn mls_process_commit(account: *const c_char, group_id_hex: *const c_char, commit: *const u8, commit_len: c_int) -> c_int {
    let (Ok(account), Ok(gid), Ok(ct)) = (cstr(account), cstr(group_id_hex), slice(commit, commit_len)) else { return ERR_BAD_ARG };
    report(
        "mls_process_commit",
        with_account(&account, false, |st| {
            let pm = protocol_message(ct)?;
            let (provider, _, group) = st.parts(&gid)?;
            let processed = group.process_message(provider, pm).map_err(|e| format!("process_message: {e:?}"))?;
            match processed.into_content() {
                ProcessedMessageContent::StagedCommitMessage(sc) => {
                    group.merge_staged_commit(provider, *sc).map_err(|e| format!("merge_staged_commit: {e:?}"))?;
                    Ok(0)
                }
                _ => Err("not a commit".into()),
            }
        }),
    )
}

/// Remove a member by leaf index. Produces a Commit for the remaining members.
#[no_mangle]
pub extern "C" fn mls_remove_member(
    account: *const c_char,
    group_id_hex: *const c_char,
    leaf_index: c_int,
    out_commit: *mut u8,
    commit_len: c_int,
) -> c_int {
    let (Ok(account), Ok(gid)) = (cstr(account), cstr(group_id_hex)) else { return ERR_BAD_ARG };
    if leaf_index < 0 {
        return ERR_BAD_ARG;
    }
    report(
        "mls_remove_member",
        with_account(&account, false, |st| {
            let (provider, signer, group) = st.parts(&gid)?;
            let (commit, _welcome, _info) = group
                .remove_members(provider, signer, &[LeafNodeIndex::new(leaf_index as u32)])
                .map_err(|e| format!("remove_members: {e:?}"))?;
            group.merge_pending_commit(provider).map_err(|e| format!("merge commit: {e:?}"))?;
            let bytes = commit.tls_serialize_detached().map_err(|e| format!("serialise: {e:?}"))?;
            Ok(write_out(&bytes, out_commit, commit_len))
        }),
    )
}

/// Current epoch of a group.
#[no_mangle]
pub extern "C" fn mls_get_epoch(account: *const c_char, group_id_hex: *const c_char) -> c_int {
    let (Ok(account), Ok(gid)) = (cstr(account), cstr(group_id_hex)) else { return ERR_BAD_ARG };
    report("mls_get_epoch", with_account(&account, false, |st| Ok(st.group(&gid)?.epoch().as_u64() as c_int)))
}

/// Members of a group as a JSON array of {"index": n, "identity": "email"}.
#[no_mangle]
pub extern "C" fn mls_get_members(account: *const c_char, group_id_hex: *const c_char, out: *mut u8, out_len: c_int) -> c_int {
    let (Ok(account), Ok(gid)) = (cstr(account), cstr(group_id_hex)) else { return ERR_BAD_ARG };
    report(
        "mls_get_members",
        with_account(&account, false, |st| {
            let members: Vec<serde_json::Value> = st
                .group(&gid)?
                .members()
                .map(|m| {
                    let identity = BasicCredential::try_from(m.credential)
                        .map(|b| String::from_utf8_lossy(b.identity()).into_owned())
                        .unwrap_or_default();
                    serde_json::json!({ "index": m.index.u32(), "identity": identity })
                })
                .collect();
            Ok(write_out(serde_json::to_string(&members).unwrap().as_bytes(), out, out_len))
        }),
    )
}

// ─── Persistence ─────────────────────────────────────────────────────────────
//
// Blob layout (all lengths big-endian u32):
//   magic "OIM1" | sig_scheme u16 | pub_len | pub | n_entries | (k_len k v_len v)*
// The signer's private key is itself an entry in the storage map (written by `signer.store()`),
// so only the public key is needed to look it up again on load.

const MAGIC: &[u8; 4] = b"OIM1";

fn put_bytes(buf: &mut Vec<u8>, d: &[u8]) {
    buf.extend_from_slice(&(d.len() as u32).to_be_bytes());
    buf.extend_from_slice(d);
}

fn take_bytes<'a>(cur: &mut &'a [u8]) -> Result<&'a [u8], String> {
    if cur.len() < 4 {
        return Err("truncated".into());
    }
    let n = u32::from_be_bytes(cur[..4].try_into().unwrap()) as usize;
    *cur = &cur[4..];
    if cur.len() < n {
        return Err("truncated".into());
    }
    let (d, rest) = cur.split_at(n);
    *cur = rest;
    Ok(d)
}

/// Serialise the whole MLS state of `account` (identity + every group) to an opaque blob.
#[no_mangle]
pub extern "C" fn mls_save_state(account: *const c_char, out: *mut u8, out_len: c_int) -> c_int {
    let Ok(account) = cstr(account) else { return ERR_BAD_ARG };
    report(
        "mls_save_state",
        with_account(&account, false, |st| {
            let mut buf = Vec::new();
            buf.extend_from_slice(MAGIC);
            buf.extend_from_slice(&(CIPHERSUITE.signature_algorithm() as u16).to_be_bytes());
            put_bytes(&mut buf, st.signer.public());
            let values = st.provider.storage.values.read().unwrap();
            buf.extend_from_slice(&(values.len() as u32).to_be_bytes());
            for (k, v) in values.iter() {
                put_bytes(&mut buf, k);
                put_bytes(&mut buf, v);
            }
            Ok(write_out(&buf, out, out_len))
        }),
    )
}

/// Restore the MLS state of `account` from a blob produced by `mls_save_state`.
/// Replaces any in-memory state for that account.
#[no_mangle]
pub extern "C" fn mls_load_state(account: *const c_char, blob: *const u8, blob_len: c_int) -> c_int {
    let (Ok(account), Ok(blob)) = (cstr(account), slice(blob, blob_len)) else { return ERR_BAD_ARG };
    let parsed = (|| -> Result<AccountState, String> {
        let mut cur = blob;
        if cur.len() < 6 || &cur[..4] != MAGIC {
            return Err("bad magic".into());
        }
        cur = &cur[6..]; // magic + scheme (scheme is fixed to CIPHERSUITE for now)
        let public = take_bytes(&mut cur)?.to_vec();
        if cur.len() < 4 {
            return Err("truncated".into());
        }
        let n = u32::from_be_bytes(cur[..4].try_into().unwrap()) as usize;
        cur = &cur[4..];
        let mut map = HashMap::with_capacity(n);
        for _ in 0..n {
            let k = take_bytes(&mut cur)?.to_vec();
            let v = take_bytes(&mut cur)?.to_vec();
            map.insert(k, v);
        }
        let provider = OimProvider { crypto: RustCrypto::default(), storage: MemoryStorage::default() };
        *provider.storage.values.write().unwrap() = map;
        let signer = SignatureKeyPair::read(provider.storage(), &public, CIPHERSUITE.signature_algorithm())
            .ok_or("signer not found in restored storage")?;
        let credential = CredentialWithKey {
            credential: BasicCredential::new(account.as_bytes().to_vec()).into(),
            signature_key: signer.public().into(),
        };
        Ok(AccountState { provider, signer, credential, groups: HashMap::new() })
    })();
    match parsed {
        Ok(st) => {
            ACCOUNTS.lock().unwrap().get_or_insert_with(HashMap::new).insert(account, st);
            0
        }
        Err(e) => {
            eprintln!("[mls_ffi] mls_load_state: {e}");
            ERR_MLS
        }
    }
}
