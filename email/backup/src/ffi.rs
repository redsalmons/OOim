//! Binary-safe C bridge for the snapshot layer. No database or vault access.

use crate::{export_file, open_file, Error, MAX_PAYLOAD_BYTES};
use std::{ffi::CStr, os::raw::c_char, panic::catch_unwind, path::Path, ptr, slice, str};
use zeroize::Zeroize;

fn status(error: Error) -> i32 {
    match error {
        Error::AuthenticationFailed => -2,
        Error::InvalidFormat => -3,
        Error::Io(_) => -4,
        Error::TooLarge => -5,
        Error::Crypto => -6,
        Error::PasswordTooShort => -7,
    }
}

// SAFETY: the C caller must provide a readable NUL-terminated path.
unsafe fn path<'a>(p: *const c_char) -> Result<&'a Path, i32> {
    if p.is_null() {
        return Err(-1);
    }
    let s = CStr::from_ptr(p).to_str().map_err(|_| -1)?;
    if s.is_empty() {
        return Err(-1);
    }
    Ok(Path::new(s))
}

// SAFETY: non-null buffers must be readable for len bytes.
unsafe fn bytes<'a>(p: *const u8, len: usize) -> Result<&'a [u8], i32> {
    if len > isize::MAX as usize {
        return Err(-1);
    }
    if len == 0 {
        return Ok(&[]);
    }
    if p.is_null() {
        return Err(-1);
    }
    Ok(slice::from_raw_parts(p, len))
}

/// See backup_ffi.h for pointer ownership and status codes.
#[no_mangle]
pub unsafe extern "C" fn oim_backup_seal_file(
    destination: *const c_char,
    payload: *const u8,
    payload_len: usize,
    password: *const u8,
    password_len: usize,
) -> i32 {
    catch_unwind(|| {
        if payload_len > MAX_PAYLOAD_BYTES {
            return -5;
        }
        let run = || {
            let destination = path(destination)?;
            let payload = bytes(payload, payload_len)?;
            let password = str::from_utf8(bytes(password, password_len)?).map_err(|_| -1)?;
            export_file(destination, payload, password).map_err(status)
        };
        run().err().unwrap_or(0)
    })
    .unwrap_or(-6)
}

#[no_mangle]
pub unsafe extern "C" fn oim_backup_open_file(
    source: *const c_char,
    password: *const u8,
    password_len: usize,
    out: *mut *mut u8,
    out_len: *mut usize,
) -> i32 {
    if out.is_null() || out_len.is_null() {
        return -1;
    }
    *out = ptr::null_mut();
    *out_len = 0;
    catch_unwind(|| {
        let run = || {
            let source = path(source)?;
            let password = str::from_utf8(bytes(password, password_len)?).map_err(|_| -1)?;
            let mut opened = open_file(source, password).map_err(status)?;
            let buffer = std::mem::take(&mut *opened.payload).into_boxed_slice();
            *out_len = buffer.len();
            *out = Box::into_raw(buffer) as *mut u8;
            Ok::<(), i32>(())
        };
        run().err().unwrap_or(0)
    })
    .unwrap_or(-6)
}

/// Only accepts the exact pointer/length returned by oim_backup_open_file, once.
#[no_mangle]
pub unsafe extern "C" fn oim_backup_free_buffer(buffer: *mut u8, len: usize) {
    if !buffer.is_null() {
        let mut owned = Box::from_raw(ptr::slice_from_raw_parts_mut(buffer, len));
        owned.zeroize();
    }
}
