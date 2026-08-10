# cmark-gfm vendor record

- Version: `0.29.0.gfm.13`
- Upstream: https://github.com/github/cmark-gfm/releases/tag/0.29.0.gfm.13
- Archive: `https://github.com/github/cmark-gfm/archive/refs/tags/0.29.0.gfm.13.tar.gz`
- SHA-256: `5abc61798ebd9de5660bc076443c07abad2b8d15dbc11094a3a79644b8ad243a`
- License: BSD-2-Clause (see `doc/COPYING`)
- Local patches: none; the only integration code is the separately maintained
  bounded renderer in `../markdown_rendering.cpp`.

The IE Viewer enables only `autolink`, `strikethrough`, `table`, `tagfilter`,
and `tasklist`. Rendering must retain cmark's safe default (never
`CMARK_OPT_UNSAFE`), validate UTF-8, and reject input, tree node/depth, and
HTML output that exceed the documented limits in `../markdown_rendering.h`.

Before a future update, run `tools/test-cmark-gfm-hardening.ps1`, review every
snapshot change in `tests/cmark-gfm/snapshots`, and replay the deterministic
extension-combination fuzz loop. Review this component quarterly and whenever
an upstream security advisory or release is published.
