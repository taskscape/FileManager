# zlib vendor record

The embedded zlib source is **1.3.2**, imported without local patches from the
official release archive:

- Source: `https://zlib.net/zlib132.zip`
- Archive SHA-256: `e8bf55f3017aa181690990cb58a994e77885da140609fc8f94abe9b65d2cae28`

The native `salamand` project compiles the embedded sources directly; it does
not load a system zlib DLL. Keep the complete in-memory API source set in the
project, including `infback.c` and `uncompr.c`, so headers and implementation
remain one verified release.

## Compatibility and update cadence

`tools/test-zlib-compatibility.ps1` builds the exact vendored C sources with
the Visual Studio developer environment. It verifies the 1.2.11 compression
vector in `tests/zlib-vectors/legacy-zlib-1.2.11.hex`, its decompression, and
rejection of the retained malformed streams in the same directory. The pull
request workflow runs this probe on x64.

Review zlib in the first maintenance window of every calendar quarter and
immediately after an upstream security advisory or release. Each upgrade must
record its source URL and SHA-256 here, refresh only intentional compatibility
vectors, retain existing corrupt-input regression files, run the probe, and
build the native project on every supported platform.
