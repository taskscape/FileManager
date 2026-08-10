# 7-Zip 26.02 vendor upgrade

The `7za` source tree was replaced from upstream 7-Zip 26.02, released 2026-06-25.
The imported source archive is `7z2602-src.7z` from
`https://github.com/ip7z/7zip/releases/download/26.02/7z2602-src.7z` with SHA-256
`C7502DD4557481F52CCF1B3E680329F1FDD207E79A25544AFEB3106325474944`.

Open Salamander keeps three compatibility seams: the ANSI file-name boundary, the
two worker-thread crash-stack hooks, and the `7zwrapper` `CompressFiles` export used
by crash-report packaging. The old corrupt-output ordering patch was deliberately
not reapplied because upstream's modern extraction path already closes/flushed
corrupted output before reporting the operation result.

Before a future 7-Zip source update, run the focused `NativeSafetyRegressionTests`
case and build the `7za.dll` and `7zwrapper` projects. Extend the checked corpus with
valid 7z, encrypted 7z, SFX, and truncated-header samples; compare item names, sizes,
methods, and extraction hashes against the prior release. Preserve minimized malformed
header, invalid-folder, and corrupt-stream fuzz regressions, asserting a clean failure
without output outside the selected destination. Record extraction snapshots for each
sample before making a new engine default.
