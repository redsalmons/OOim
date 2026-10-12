// End-to-end test of the C ABI: three parties, group creation, join, encrypt/decrypt,
// commit processing, and state persistence.

use std::ffi::CString;
use std::os::raw::c_int;

use mls_ffi::*;

fn c(s: &str) -> CString { CString::new(s).unwrap() }

fn buf(n: usize) -> Vec<u8> { vec![0u8; n] }

fn ok(rc: c_int, what: &str) -> usize {
    assert!(rc >= 0, "{what} failed rc={rc}");
    rc as usize
}

const AAD: &[u8] = b"{\"x_message_id\":\"<m>\"}";

unsafe fn enc(who: &CString, gid: &CString, msg: &[u8], aad: &[u8], out: *mut u8, n: c_int) -> c_int {
    mls_encrypt(who.as_ptr(), gid.as_ptr(), aad.as_ptr(), aad.len() as c_int, msg.as_ptr(), msg.len() as c_int, out, n)
}

unsafe fn dec(who: &CString, gid: &CString, ct: &[u8], aad: &[u8], out: *mut u8, n: c_int) -> (c_int, String) {
    let mut sender = vec![0u8; 256];
    let rc = mls_decrypt(who.as_ptr(), gid.as_ptr(), aad.as_ptr(), aad.len() as c_int, ct.as_ptr(), ct.len() as c_int, out, n, sender.as_mut_ptr(), 256);
    let end = sender.iter().position(|&b| b == 0).unwrap_or(sender.len());
    let s = if rc >= 0 { String::from_utf8_lossy(&sender[..end]).into_owned() } else { String::new() };
    (rc, s)
}

#[test]
fn three_party_roundtrip() {
    let (a, b, cc) = (c("a@163.com"), c("b@qq.com"), c("c@outlook.com"));
    unsafe {
        for acc in [&a, &b, &cc] { ok(mls_init(acc.as_ptr()), "init"); }

        // B and C publish KeyPackages
        let mut kp_b = buf(4096); let n = ok(mls_generate_key_package(b.as_ptr(), kp_b.as_mut_ptr(), 4096), "kp b"); kp_b.truncate(n);
        let mut kp_c = buf(4096); let n = ok(mls_generate_key_package(cc.as_ptr(), kp_c.as_mut_ptr(), 4096), "kp c"); kp_c.truncate(n);

        // A creates the group
        let mut gid = buf(128); let n = ok(mls_create_group(a.as_ptr(), gid.as_mut_ptr(), 128), "create"); gid.truncate(n);
        let gid_s = String::from_utf8(gid.clone()).unwrap();
        let gid_c = c(&gid_s);
        assert_eq!(mls_get_epoch(a.as_ptr(), gid_c.as_ptr()), 0);

        // A adds B and C in one commit
        use base64::Engine;
        let e = base64::engine::general_purpose::STANDARD;
        let kps_json = c(&serde_json::to_string(&[e.encode(&kp_b), e.encode(&kp_c)]).unwrap());
        let mut welcome = buf(65536); let mut wl = 0;
        let mut commit = buf(65536); let mut cl = 0;
        ok(mls_add_members(a.as_ptr(), gid_c.as_ptr(), kps_json.as_ptr(),
            welcome.as_mut_ptr(), 65536, &mut wl, commit.as_mut_ptr(), 65536, &mut cl), "add_members");
        welcome.truncate(wl as usize);
        assert_eq!(mls_get_epoch(a.as_ptr(), gid_c.as_ptr()), 1);

        // A exports tree; B and C join
        let mut tree = buf(65536); let n = ok(mls_export_ratchet_tree(a.as_ptr(), gid_c.as_ptr(), tree.as_mut_ptr(), 65536), "tree"); tree.truncate(n);
        for who in [&b, &cc] {
            let mut g = buf(128);
            let n = ok(mls_join_group(who.as_ptr(), welcome.as_ptr(), welcome.len() as c_int, tree.as_ptr(), tree.len() as c_int, g.as_mut_ptr(), 128), "join");
            g.truncate(n);
            assert_eq!(g, gid, "joined group id must match");
            assert_eq!(mls_get_epoch(who.as_ptr(), gid_c.as_ptr()), 1);
        }

        // Members list from B's view
        let mut m = buf(4096); let n = ok(mls_get_members(b.as_ptr(), gid_c.as_ptr(), m.as_mut_ptr(), 4096), "members"); m.truncate(n);
        let members: Vec<serde_json::Value> = serde_json::from_slice(&m).unwrap();
        let ids: Vec<&str> = members.iter().map(|v| v["identity"].as_str().unwrap()).collect();
        assert_eq!(ids, ["a@163.com", "b@qq.com", "c@outlook.com"]);

        // A -> everyone
        let msg = b"hello group";
        let mut ct = buf(65536); let n = ok(enc(&a, &gid_c, msg, AAD, ct.as_mut_ptr(), 65536), "enc a"); ct.truncate(n);
        for who in [&b, &cc] {
            let mut pt = buf(4096);
            let n = ok(dec(&who, &gid_c, &ct, AAD, pt.as_mut_ptr(), 4096).0, "dec");
            assert_eq!(&pt[..n], msg);
        }

        // B -> everyone (A and C decrypt)
        let msg2 = b"reply from b";
        let mut ct2 = buf(65536); let n = ok(enc(&b, &gid_c, msg2, AAD, ct2.as_mut_ptr(), 65536), "enc b"); ct2.truncate(n);
        for who in [&a, &cc] {
            let mut pt = buf(4096);
            let n = ok(dec(&who, &gid_c, &ct2, AAD, pt.as_mut_ptr(), 4096).0, "dec2");
            assert_eq!(&pt[..n], msg2);
        }

        // Authenticated sender comes from the MLS credential, not from the body
        let msg_s = b"who am i";
        let mut ct_s = buf(65536); let n = ok(enc(&cc, &gid_c, msg_s, AAD, ct_s.as_mut_ptr(), 65536), "enc c"); ct_s.truncate(n);
        let mut pt = buf(4096);
        let (n, sender) = dec(&a, &gid_c, &ct_s, AAD, pt.as_mut_ptr(), 4096);
        assert!(n >= 0, "dec sender");
        assert_eq!(sender, "c@outlook.com");

        // A modified envelope (different AAD) must be rejected
        let mut ct_t = buf(65536); let n = ok(enc(&cc, &gid_c, msg_s, AAD, ct_t.as_mut_ptr(), 65536), "enc t"); ct_t.truncate(n);
        assert!(dec(&a, &gid_c, &ct_t, b"{\"x_message_id\":\"<other>\"}", pt.as_mut_ptr(), 4096).0 < 0, "aad mismatch must fail");

        // PCS: B proposes a self-update, A (owner) commits it, everyone advances
        let mut prop = buf(65536); let n = ok(mls_propose_self_update(b.as_ptr(), gid_c.as_ptr(), prop.as_mut_ptr(), 65536), "propose"); prop.truncate(n);
        assert_eq!(mls_process_commit(a.as_ptr(), gid_c.as_ptr(), prop.as_ptr(), prop.len() as c_int), 1, "owner queues proposal");
        assert_eq!(mls_process_commit(cc.as_ptr(), gid_c.as_ptr(), prop.as_ptr(), prop.len() as c_int), 1, "member queues proposal");
        let mut cm = buf(65536); let n = ok(mls_commit_pending(a.as_ptr(), gid_c.as_ptr(), cm.as_mut_ptr(), 65536), "commit pending"); cm.truncate(n);
        ok(mls_process_commit(b.as_ptr(), gid_c.as_ptr(), cm.as_ptr(), cm.len() as c_int), "b merges");
        ok(mls_process_commit(cc.as_ptr(), gid_c.as_ptr(), cm.as_ptr(), cm.len() as c_int), "c merges");
        let ep = mls_get_epoch(a.as_ptr(), gid_c.as_ptr());
        assert_eq!(mls_get_epoch(b.as_ptr(), gid_c.as_ptr()), ep);
        assert_eq!(mls_get_epoch(cc.as_ptr(), gid_c.as_ptr()), ep);

        // Owner self-update
        let mut cm2 = buf(65536); let n = ok(mls_self_update(a.as_ptr(), gid_c.as_ptr(), cm2.as_mut_ptr(), 65536), "self_update"); cm2.truncate(n);
        ok(mls_process_commit(b.as_ptr(), gid_c.as_ptr(), cm2.as_ptr(), cm2.len() as c_int), "b merges 2");
        ok(mls_process_commit(cc.as_ptr(), gid_c.as_ptr(), cm2.as_ptr(), cm2.len() as c_int), "c merges 2");
        assert_eq!(mls_get_epoch(cc.as_ptr(), gid_c.as_ptr()), ep + 1);

        // Still talking after the updates
        let msg_u = b"post update";
        let mut ct_u = buf(65536); let n = ok(enc(&b, &gid_c, msg_u, AAD, ct_u.as_mut_ptr(), 65536), "enc u"); ct_u.truncate(n);
        let n = ok(dec(&a, &gid_c, &ct_u, AAD, pt.as_mut_ptr(), 4096).0, "dec u");
        assert_eq!(&pt[..n], msg_u);

        // Signature key helpers: KeyPackage carries the account's MLS signature key
        let mut sk = buf(256); let n = ok(mls_signature_key(b.as_ptr(), sk.as_mut_ptr(), 256), "sigkey"); sk.truncate(n);
        let mut kp_b2 = buf(4096); let n = ok(mls_generate_key_package(b.as_ptr(), kp_b2.as_mut_ptr(), 4096), "kp b2"); kp_b2.truncate(n);
        let mut sk2 = buf(256); let n = ok(mls_key_package_signature_key(kp_b2.as_ptr(), kp_b2.len() as c_int, sk2.as_mut_ptr(), 256), "kp sigkey"); sk2.truncate(n);
        assert_eq!(sk, sk2);

        // Replay must fail (forward secrecy / no key reuse)
        let mut pt = buf(4096);
        assert!(dec(&a, &gid_c, &ct2, AAD, pt.as_mut_ptr(), 4096).0 < 0, "replay must be rejected");

        // Persist C, wipe, restore, still able to talk
        let mut blob = buf(1 << 20); let n = ok(mls_save_state(cc.as_ptr(), blob.as_mut_ptr(), 1 << 20), "save"); blob.truncate(n);
        ok(mls_load_state(cc.as_ptr(), blob.as_ptr(), blob.len() as c_int), "load");
        let msg3 = b"after restore";
        let mut ct3 = buf(65536); let n = ok(enc(&cc, &gid_c, msg3, AAD, ct3.as_mut_ptr(), 65536), "enc c"); ct3.truncate(n);
        let mut pt = buf(4096);
        let n = ok(dec(&a, &gid_c, &ct3, AAD, pt.as_mut_ptr(), 4096).0, "dec3");
        assert_eq!(&pt[..n], msg3);

        // A adds a 4th member later; B processes the commit and advances epoch
        let d = c("d@gmail.com");
        ok(mls_init(d.as_ptr()), "init d");
        let mut kp_d = buf(4096); let n = ok(mls_generate_key_package(d.as_ptr(), kp_d.as_mut_ptr(), 4096), "kp d"); kp_d.truncate(n);
        let kps_json = c(&serde_json::to_string(&[e.encode(&kp_d)]).unwrap());
        let mut w2 = buf(65536); let mut wl2 = 0; let mut c2 = buf(65536); let mut cl2 = 0;
        ok(mls_add_members(a.as_ptr(), gid_c.as_ptr(), kps_json.as_ptr(), w2.as_mut_ptr(), 65536, &mut wl2, c2.as_mut_ptr(), 65536, &mut cl2), "add d");
        c2.truncate(cl2 as usize);
        ok(mls_process_commit(b.as_ptr(), gid_c.as_ptr(), c2.as_ptr(), c2.len() as c_int), "b commit");
        ok(mls_process_commit(cc.as_ptr(), gid_c.as_ptr(), c2.as_ptr(), c2.len() as c_int), "c commit");
        let ep_final = mls_get_epoch(a.as_ptr(), gid_c.as_ptr());
        assert_eq!(ep_final, ep + 2);
        assert_eq!(mls_get_epoch(b.as_ptr(), gid_c.as_ptr()), ep_final);
        assert_eq!(mls_get_epoch(cc.as_ptr(), gid_c.as_ptr()), ep_final);
    }
}

/// Sequential app messages from ONE sender must decrypt in order — file chunks
/// are 5+ consecutive encrypts; a reused generation shows up as SecretReuseError.
#[test]
fn sequential_sends_from_one_member() {
    let (a, b, cc) = (c("s1@x"), c("s2@x"), c("s3@x"));
    unsafe {
        for who in [&a, &b, &cc] { ok(mls_init(who.as_ptr()), "init"); }

        let mut kp_b = buf(4096); let n = ok(mls_generate_key_package(b.as_ptr(), kp_b.as_mut_ptr(), 4096), "kp b"); kp_b.truncate(n);
        let mut kp_c = buf(4096); let n = ok(mls_generate_key_package(cc.as_ptr(), kp_c.as_mut_ptr(), 4096), "kp c"); kp_c.truncate(n);

        let mut gid = buf(128); let n = ok(mls_create_group(a.as_ptr(), gid.as_mut_ptr(), 128), "create"); gid.truncate(n);
        let gid_c = c(&String::from_utf8(gid.clone()).unwrap());

        use base64::Engine;
        let e = base64::engine::general_purpose::STANDARD;
        let kps_json = c(&serde_json::to_string(&[e.encode(&kp_b), e.encode(&kp_c)]).unwrap());
        let mut welcome = buf(65536); let mut wl = 0;
        let mut commit = buf(65536); let mut cl = 0;
        ok(mls_add_members(a.as_ptr(), gid_c.as_ptr(), kps_json.as_ptr(),
            welcome.as_mut_ptr(), 65536, &mut wl, commit.as_mut_ptr(), 65536, &mut cl), "add_members");
        welcome.truncate(wl as usize);

        let mut tree = buf(65536); let n = ok(mls_export_ratchet_tree(a.as_ptr(), gid_c.as_ptr(), tree.as_mut_ptr(), 65536), "tree"); tree.truncate(n);
        for who in [&b, &cc] {
            let mut g = buf(128);
            ok(mls_join_group(who.as_ptr(), welcome.as_ptr(), welcome.len() as c_int, tree.as_ptr(), tree.len() as c_int, g.as_mut_ptr(), 128), "join");
        }

        // 6 consecutive app messages from A — meta + 5 chunks, as in a file send.
        // Simulate the app: persist state after every encrypt (save_state), and a
        // restart-style load_state before each encrypt as well.
        for i in 0..6u8 {
            let msg = format!("chunk-{i}");
            let mut blob = buf(4 << 20);
            let n = ok(mls_save_state(a.as_ptr(), blob.as_mut_ptr(), (4 << 20) as c_int), "save");
            ok(mls_load_state(a.as_ptr(), blob.as_ptr(), n as c_int), "load");

            let mut ct = buf(65536);
            let n = ok(enc(&a, &gid_c, msg.as_bytes(), AAD, ct.as_mut_ptr(), ct.len() as c_int), "enc");
            ct.truncate(n);
            for who in [&b, &cc] {
                let mut pt = buf(4096);
                let r = dec(who, &gid_c, &ct, AAD, pt.as_mut_ptr(), pt.len() as c_int).0;
                assert!(r >= 0, "decrypt msg {i} failed rc={r}");
                assert_eq!(&pt[..r as usize], msg.as_bytes());
            }
        }
    }
}
