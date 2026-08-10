# bzip2 vendor record

The main executable builds the upstream bzip2 1.0.8 library sources in this
directory with `BZ_NO_STDIO`. `src/salbzip2.cpp` is the separate host adapter;
it retains the product's `CSalamanderBZIP2Abstract` streaming contract.

- Upstream release: [bzip2 1.0.8](https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz), 13 July 2019.
- Upstream SHA-512: `083f5e675d73f3233c7930ebe20425a533feedeaaa9d8cc86831312a6581cefbe6ed0d08d2fa89be81082f2a5abdabca8b3c080bf97218a1bd59dc118a30b9f3`.
- Verification: the release archive SHA-512 was checked against
  `https://sourceware.org/pub/bzip2/sha512.sum` before the source refresh.
- Local patches: none. `internal_e.cpp` is a FileManager integration shim and
  is deliberately outside the imported upstream source set.
- Review cadence: review every calendar quarter and immediately on an upstream
  security advisory or release.

`tools/test-bzip2-compatibility.ps1` builds these checked-in sources, decodes
the retained golden archives through the streaming API, rejects a truncation
fixture, and replays the checked-in malformed-input fuzz corpus.
