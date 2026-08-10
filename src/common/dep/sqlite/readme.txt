# SQLite vendor and recovery record

The vendored amalgamation is SQLite 3.53.4 (2026-07-24), imported from
`https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip`.

| Item | SHA3-256 |
| --- | --- |
| Official amalgamation archive | `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e` |
| Imported `sqlite3.c` | `67f423e9ebbbdc473cbc4772c872ee6b89f31fde4ed0279a5c25d5f65c043a16` |

Both `sqlite3.c` and `plugins/shared/sqlite/sqlite3.h` must come from the same
verified archive. `sqlite3_compileoption_get()` remains available; the
effective build options are deliberately recorded in
`src/vcxproj/sqlite/sqlite_base.props`:

- `SQLITE_DQS=0` rejects legacy double-quoted string literals.
- `SQLITE_ENABLE_API_ARMOR` turns API misuse into checked errors.
- `SQLITE_DEFAULT_SYNCHRONOUS=2`, `SQLITE_DEFAULT_WAL_SYNCHRONOUS=2`, and
  `SQLITE_DEFAULT_WAL_AUTOCHECKPOINT=1000` keep the default and WAL profiles
  fully synchronous, with an explicit checkpoint threshold.

## Database ownership and recovery policy

The current product only opens Google Drive's `sync_config.db` read-only to
discover its configured path. It is third-party data: FileManager must not run
integrity checks, checkpoints, migrations, or recovery writes against it.

If FileManager adds an owned SQLite database, its open path must first set
`journal_mode=WAL`, `synchronous=FULL`, and `wal_autocheckpoint=1000`, then
perform every mutation in a `BEGIN IMMEDIATE` transaction. On startup and after
an interrupted write, it must run `PRAGMA integrity_check`. An `ok` result is
required before the database is used. For `SQLITE_CORRUPT`, `SQLITE_NOTADB`, or
a non-`ok` integrity result, close the handle without writing, preserve the
database and its `-wal`/`-shm` companions for diagnosis, and create a fresh
store only through an explicit recovery flow. Never auto-repair or overwrite a
corrupt database in place.

`tools/test-sqlite-recovery.ps1` executes this contract against the built DLL:
it proves that closing an uncommitted WAL transaction preserves only committed
rows after reopen, and that a deliberately corrupted b-tree page fails the
integrity check. Run it after the Debug x64 SQLite build.
