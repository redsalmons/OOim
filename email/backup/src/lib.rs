//! Local, password-encrypted container for full backups.
//!
//! This layer knows nothing about Signal, MLS, SQLite, or the credential store.
//! A future snapshot layer supplies the payload and applies authenticated data.
//! Opening a container never modifies the application's state.

mod ffi;

use argon2::{Algorithm, Argon2, Params, Version};
use chacha20poly1305::{
    aead::{Aead, KeyInit, Payload},
    XChaCha20Poly1305, XNonce,
};
use rand_core::{OsRng, RngCore};
use serde::{Deserialize, Serialize};
use std::{
    fmt,
    fs::File,
    io::{self, Read, Write},
    path::Path,
    time::{SystemTime, UNIX_EPOCH},
};
use zeroize::Zeroizing;

// Header: magic/version (8), Argon2id m/t/p (12), salt (16), nonce (24).
// Everything in this header is authenticated as AEAD associated data.
const MAGIC: &[u8; 8] = b"OIMBK1\r\n";
const MEMORY_KIB: u32 = 64 * 1024;
const ITERATIONS: u32 = 3;
const PARALLELISM: u32 = 1;
const HEADER_LEN: usize = 60;
const TAG_LEN: usize = 16;
const MAX_MANIFEST_LEN: usize = 4096;
// This initial container implementation is in-memory. Larger full snapshots
// need a separately versioned streaming format, not unchecked allocations.
pub const MAX_PAYLOAD_BYTES: usize = 256 * 1024 * 1024;
const MAX_FILE_BYTES: usize = HEADER_LEN + TAG_LEN + 4 + MAX_MANIFEST_LEN + MAX_PAYLOAD_BYTES;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum Scope {
    #[serde(rename = "full")]
    Full,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Manifest {
    pub format: u32,
    pub scope: Scope,
    pub created_at_unix: u64,
}

// Deliberately no Debug implementation: payloads contain private keys/secrets.
pub struct OpenedBackup {
    pub manifest: Manifest,
    pub payload: Zeroizing<Vec<u8>>,
}

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    InvalidFormat,
    AuthenticationFailed,
    PasswordTooShort,
    TooLarge,
    Crypto,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Self::Io(_) => "Backup file could not be read or written",
            Self::InvalidFormat => "Unsupported or invalid backup format",
            Self::AuthenticationFailed => "Incorrect password or damaged backup",
            Self::PasswordTooShort => "Backup password must contain at least 8 characters",
            Self::TooLarge => "Backup exceeds the container size limit",
            Self::Crypto => "Backup cryptography failed",
        })
    }
}

impl std::error::Error for Error {}

impl From<io::Error> for Error {
    fn from(error: io::Error) -> Self {
        Self::Io(error)
    }
}

fn derive_key(password: &str, salt: &[u8]) -> Result<Zeroizing<[u8; 32]>, Error> {
    let params =
        Params::new(MEMORY_KIB, ITERATIONS, PARALLELISM, Some(32)).map_err(|_| Error::Crypto)?;
    let mut key = Zeroizing::new([0u8; 32]);
    Argon2::new(Algorithm::Argon2id, Version::V0x13, params)
        .hash_password_into(password.as_bytes(), salt, key.as_mut())
        .map_err(|_| Error::Crypto)?;
    Ok(key)
}

fn read_u32(bytes: &[u8]) -> u32 {
    u32::from_le_bytes(bytes.try_into().expect("internal fixed-width field"))
}

/// Encrypt both metadata and payload. No account information or scope is public.
pub fn seal(payload: &[u8], password: &str) -> Result<Vec<u8>, Error> {
    if password.chars().count() < 8 {
        return Err(Error::PasswordTooShort);
    }
    if payload.len() > MAX_PAYLOAD_BYTES {
        return Err(Error::TooLarge);
    }
    let manifest = Manifest {
        format: 1,
        scope: Scope::Full,
        created_at_unix: SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(|_| Error::InvalidFormat)?
            .as_secs(),
    };
    let metadata = serde_json::to_vec(&manifest).map_err(|_| Error::InvalidFormat)?;
    let mut plaintext = Zeroizing::new(Vec::with_capacity(4 + metadata.len() + payload.len()));
    plaintext.extend_from_slice(&(metadata.len() as u32).to_le_bytes());
    plaintext.extend_from_slice(&metadata);
    plaintext.extend_from_slice(payload);

    let mut header = [0u8; HEADER_LEN];
    header[..8].copy_from_slice(MAGIC);
    header[8..12].copy_from_slice(&MEMORY_KIB.to_le_bytes());
    header[12..16].copy_from_slice(&ITERATIONS.to_le_bytes());
    header[16..20].copy_from_slice(&PARALLELISM.to_le_bytes());
    OsRng
        .try_fill_bytes(&mut header[20..])
        .map_err(|_| Error::Crypto)?;
    let key = derive_key(password, &header[20..36])?;
    let ciphertext = XChaCha20Poly1305::new_from_slice(key.as_ref())
        .map_err(|_| Error::Crypto)?
        .encrypt(
            XNonce::from_slice(&header[36..60]),
            Payload {
                msg: &plaintext,
                aad: &header,
            },
        )
        .map_err(|_| Error::Crypto)?;
    let mut result = Vec::with_capacity(HEADER_LEN + ciphertext.len());
    result.extend_from_slice(&header);
    result.extend_from_slice(&ciphertext);
    Ok(result)
}

/// Authenticate/decrypt before reading scope. No caller-supplied scope is accepted.
pub fn open(bytes: &[u8], password: &str) -> Result<OpenedBackup, Error> {
    if bytes.len() > MAX_FILE_BYTES {
        return Err(Error::TooLarge);
    }
    if bytes.len() < HEADER_LEN + TAG_LEN + 4 || &bytes[..8] != MAGIC {
        return Err(Error::InvalidFormat);
    }
    // v1 accepts one exact KDF profile. Never let a file request arbitrary
    // memory/CPU usage, even before its authentication tag can be checked.
    if read_u32(&bytes[8..12]) != MEMORY_KIB
        || read_u32(&bytes[12..16]) != ITERATIONS
        || read_u32(&bytes[16..20]) != PARALLELISM
    {
        return Err(Error::InvalidFormat);
    }
    let key = derive_key(password, &bytes[20..36])?;
    let plaintext = Zeroizing::new(
        XChaCha20Poly1305::new_from_slice(key.as_ref())
            .map_err(|_| Error::Crypto)?
            .decrypt(
                XNonce::from_slice(&bytes[36..60]),
                Payload {
                    msg: &bytes[HEADER_LEN..],
                    aad: &bytes[..HEADER_LEN],
                },
            )
            .map_err(|_| Error::AuthenticationFailed)?,
    );
    if plaintext.len() < 4 {
        return Err(Error::InvalidFormat);
    }
    let manifest_len = read_u32(&plaintext[..4]) as usize;
    if manifest_len > MAX_MANIFEST_LEN || manifest_len > plaintext.len() - 4 {
        return Err(Error::InvalidFormat);
    }
    let manifest: Manifest = serde_json::from_slice(&plaintext[4..4 + manifest_len])
        .map_err(|_| Error::InvalidFormat)?;
    if manifest.format != 1 {
        return Err(Error::InvalidFormat);
    }
    let payload = &plaintext[4 + manifest_len..];
    if payload.len() > MAX_PAYLOAD_BYTES {
        return Err(Error::TooLarge);
    }
    Ok(OpenedBackup {
        manifest,
        payload: Zeroizing::new(payload.to_vec()),
    })
}

/// Write only ciphertext to disk, then publish the finished file atomically.
/// An existing destination is never overwritten. Caller chooses a local path.
pub fn export_file(destination: &Path, payload: &[u8], password: &str) -> Result<(), Error> {
    let ciphertext = seal(payload, password)?;
    let parent = destination
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    let mut temporary = tempfile::NamedTempFile::new_in(parent)?;
    temporary.write_all(&ciphertext)?;
    temporary.as_file().sync_all()?;
    temporary
        .persist_noclobber(destination)
        .map_err(|error| Error::Io(error.error))?;
    Ok(())
}

/// Read a local file with a bounded allocation. This does not restore a database.
pub fn open_file(source: &Path, password: &str) -> Result<OpenedBackup, Error> {
    let file = File::open(source)?;
    if file.metadata()?.len() > MAX_FILE_BYTES as u64 {
        return Err(Error::TooLarge);
    }
    let mut bytes = Vec::new();
    file.take(MAX_FILE_BYTES as u64 + 1)
        .read_to_end(&mut bytes)?;
    open(&bytes, password)
}

/// Preview is available only after successful password authentication.
pub fn inspect_file(source: &Path, password: &str) -> Result<Manifest, Error> {
    Ok(open_file(source, password)?.manifest)
}

#[cfg(test)]
mod tests {
    use super::*;

    const PASSWORD: &str = "test-only recovery password";
    const DATA: &[u8] = b"private identity key\x00binary state\xff";

    #[test]
    fn full_backup_round_trip_preserves_binary_payload() {
        let bytes = seal(DATA, PASSWORD).unwrap();
        assert_eq!(&bytes[..8], MAGIC);
        assert!(!bytes.windows(DATA.len()).any(|window| window == DATA));
        let restored = open(&bytes, PASSWORD).unwrap();
        assert_eq!(restored.manifest.scope, Scope::Full);
        assert_eq!(&*restored.payload, DATA);
    }

    #[test]
    fn random_salt_and_nonce_produce_different_files() {
        let a = seal(DATA, PASSWORD).unwrap();
        let b = seal(DATA, PASSWORD).unwrap();
        assert_ne!(&a[20..36], &b[20..36]);
        assert_ne!(&a[36..60], &b[36..60]);
        assert_ne!(a, b);
    }

    #[test]
    fn wrong_password_and_tampering_never_return_plaintext() {
        let bytes = seal(DATA, PASSWORD).unwrap();
        assert!(matches!(
            open(&bytes, "wrong password"),
            Err(Error::AuthenticationFailed)
        ));
        for offset in [20, 36, HEADER_LEN, bytes.len() - 1] {
            let mut damaged = bytes.clone();
            damaged[offset] ^= 1;
            assert!(matches!(
                open(&damaged, PASSWORD),
                Err(Error::AuthenticationFailed)
            ));
        }
        let mut appended = bytes.clone();
        appended.push(0);
        assert!(matches!(
            open(&appended, PASSWORD),
            Err(Error::AuthenticationFailed)
        ));
    }

    #[test]
    fn unsupported_header_and_truncated_files_are_rejected() {
        let bytes = seal(DATA, PASSWORD).unwrap();
        for len in [0, 7, 8, HEADER_LEN, HEADER_LEN + TAG_LEN, bytes.len() - 1] {
            assert!(open(&bytes[..len], PASSWORD).is_err());
        }
        for offset in [0, 5, 8, 12, 16] {
            let mut damaged = bytes.clone();
            damaged[offset] ^= 1;
            assert!(matches!(
                open(&damaged, PASSWORD),
                Err(Error::InvalidFormat)
            ));
        }
    }

    #[test]
    fn local_export_preview_and_open_preserve_existing_files() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("test.oimbak");
        export_file(&path, DATA, PASSWORD).unwrap();
        let original = std::fs::read(&path).unwrap();
        assert_eq!(inspect_file(&path, PASSWORD).unwrap().scope, Scope::Full);
        assert_eq!(&*open_file(&path, PASSWORD).unwrap().payload, DATA);
        assert!(export_file(&path, b"replacement", PASSWORD).is_err());
        assert_eq!(std::fs::read(&path).unwrap(), original);
        assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 1);
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            assert_eq!(
                std::fs::metadata(&path).unwrap().permissions().mode() & 0o777,
                0o600
            );
        }
    }

    #[test]
    fn short_password_does_not_create_a_file() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("test.oimbak");
        assert!(matches!(
            export_file(&path, DATA, "short"),
            Err(Error::PasswordTooShort)
        ));
        assert!(!path.exists());
        assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 0);
    }
}
