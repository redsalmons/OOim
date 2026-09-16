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
        let mut ct = buf(65536); let n = ok(mls_encrypt(a.as_ptr(), gid_c.as_ptr(), msg.as_ptr(), msg.len() as c_int, ct.as_mut_ptr(), 65536), "enc a"); ct.truncate(n);
        for who in [&b, &cc] {
            let mut pt = buf(4096);
            let n = ok(mls_decrypt(who.as_ptr(), gid_c.as_ptr(), ct.as_ptr(), ct.len() as c_int, pt.as_mut_ptr(), 4096), "dec");
            assert_eq!(&pt[..n], msg);
        }

        // B -> everyone (A and C decrypt)
        let msg2 = b"reply from b";
        let mut ct2 = buf(65536); let n = ok(mls_encrypt(b.as_ptr(), gid_c.as_ptr(), msg2.as_ptr(), msg2.len() as c_int, ct2.as_mut_ptr(), 65536), "enc b"); ct2.truncate(n);
        for who in [&a, &cc] {
            let mut pt = buf(4096);
            let n = ok(mls_decrypt(who.as_ptr(), gid_c.as_ptr(), ct2.as_ptr(), ct2.len() as c_int, pt.as_mut_ptr(), 4096), "dec2");
            assert_eq!(&pt[..n], msg2);
        }

        // Replay must fail (forward secrecy / no key reuse)
        let mut pt = buf(4096);
        assert!(mls_decrypt(a.as_ptr(), gid_c.as_ptr(), ct2.as_ptr(), ct2.len() as c_int, pt.as_mut_ptr(), 4096) < 0, "replay must be rejected");

        // Persist C, wipe, restore, still able to talk
        let mut blob = buf(1 << 20); let n = ok(mls_save_state(cc.as_ptr(), blob.as_mut_ptr(), 1 << 20), "save"); blob.truncate(n);
        ok(mls_load_state(cc.as_ptr(), blob.as_ptr(), blob.len() as c_int), "load");
        let msg3 = b"after restore";
        let mut ct3 = buf(65536); let n = ok(mls_encrypt(cc.as_ptr(), gid_c.as_ptr(), msg3.as_ptr(), msg3.len() as c_int, ct3.as_mut_ptr(), 65536), "enc c"); ct3.truncate(n);
        let mut pt = buf(4096);
        let n = ok(mls_decrypt(a.as_ptr(), gid_c.as_ptr(), ct3.as_ptr(), ct3.len() as c_int, pt.as_mut_ptr(), 4096), "dec3");
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
        assert_eq!(mls_get_epoch(a.as_ptr(), gid_c.as_ptr()), 2);
        assert_eq!(mls_get_epoch(b.as_ptr(), gid_c.as_ptr()), 2);
        assert_eq!(mls_get_epoch(cc.as_ptr(), gid_c.as_ptr()), 2);
    }
}
