# Local full-backup container and snapshot layer

The Rust container and C++ snapshot layer are linked into `email_core` through
CMake. They are not yet connected to Flutter configuration pages or application
cutover. The Rust layer has no network or operating-system key-store API.
The C++ adapter reads configuration and credentials from a stopped core.

- Only full backups are supported. `scope: full` lives inside the encrypted
  manifest; export does not accept a scope selector. Minimal backups are cancelled.
- Argon2id v1.3: 64 MiB, 3 iterations, parallelism 1, random 16-byte salt.
- XChaCha20-Poly1305: 32-byte key, random 24-byte nonce, 16-byte tag.
- The entire header is authenticated; unsupported KDF parameters are rejected
  before deriving a key. The manifest is available only after authentication.
- Export requires a password of at least 8 characters. This is a minimum input
  rule, not a guarantee against password guessing.
- Only ciphertext is written, using a temporary file in the destination directory
  and atomic publication without overwriting existing files. Unix files use 0600.
- Derived keys and successfully decrypted payload buffers are zeroized on drop.
  The caller owns its original password/payload and must manage their lifetime.
- Initial limit: 256 MiB of payload, processed in memory. A streaming format is
  needed before claiming support for larger full backups.

Binary layout (all integers little-endian):

```text
8 bytes   OIMBK1\r\n
u32       memory KiB (65536)
u32       iterations (3)
u32       parallelism (1)
16 bytes  salt
24 bytes  nonce
remaining authenticated ciphertext + 16-byte tag
```

Decrypted content: `u32 manifest_length`, UTF-8 JSON manifest
(`format: 1`, `scope: full`, `created_at_unix`), then opaque payload.
The application must additionally validate snapshot schema and contents before
writing anything to its database. Container authentication alone does not make
imported SQL, paths, or restored protocol state safe to execute.

`snapshot.cpp` implements snapshot payload version 1, encoded in CBOR:

- `config`: the complete configuration object; inline credentials are scrubbed.
- `credentials`: explicit entries for every account email and `email:refresh`.
  Missing entries fail validation; empty strings mean no saved credential.
- `tables`: known user tables, column metadata and rows. SQLite INTEGER, REAL,
  TEXT, BLOB and NULL are preserved, including 64-bit IDs and embedded NUL bytes.
  Row IDs and body message relationships are copied without re-threading.
  `task` and its sequence counter are excluded; other ID high-water marks remain.
- `originals`: bytes of every `file_transfer.original_path`, keyed by the
  transfer row ID. Missing/unreadable/changing originals abort export rather than
  producing a silently incomplete backup. Received attachment chunks are already
  in `file_chunk`; reassembled downloads need not be archived a second time.

Native operations:

- `exportSnapshot`: accepts an idle source DB, config and a credential map.
  Caller must have drained all protocol workers and frozen configuration/files.
  The SQLite transaction ensures one DB view but cannot quiesce protocol work.
- `exportStoppedSnapshot`: reads config and the corresponding vault through
  `keychain_store::snapshot_secrets`. Fails if the global DB is still open.
  It does not change the active vault path or replace/generate a master key.
- `readSnapshot`: authenticates, decodes and validates without changing live state.
- `restoreTables`: restores into a schema-matching empty, isolated database in
  one transaction. It does not execute archived SQL or overwrite populated tables.
  The target may use its own SQLCipher key.

Application integration remains necessary: drain the workers, obtain the keyed
source connection, stage attachment bytes and pass the row-ID-to-new-path map,
rewrite the configured data directory, commit credentials/config, atomically
activate the staged data, and reset runtime caches. This layer does not yet write
restored attachment files or activate restored sessions. Old ratchet snapshots
must not simply be resumed for sending without the planned protocol-state checks.

Run isolated tests (no real account or Keychain access):

```sh
cargo test --manifest-path email/backup/Cargo.toml --locked
cmake -S email -B email/build
cmake --build email/build --target backup_snapshot_test
ctest --test-dir email/build -R '^backup_snapshot$' --output-on-failure
```

The native fixture checks history/state/credential/file roundtrips, exact message
relationships, a target encrypted with a different device key, path remapping,
task exclusion, rollback, schema mismatch and missing credentials/originals.
