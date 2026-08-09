# Crash-report transmission

This document describes the data sent only after the user explicitly chooses
**Send Report** in the Open Salamander Bug Reporter. The consent text in that
dialog identifies the archive, its destination, its use of HTTPS, and the
**View Report** and **Do Not Send Report** alternatives. The application does
not upload a report automatically.

## Transport and endpoint

| Item | Value |
|---|---|
| Protocol | HTTPS, using Windows WinHTTP and SChannel certificate-chain and hostname validation |
| Method and format | `POST` with one `multipart/form-data` part named `taskscapefile` |
| Versioned endpoint expected by the service | `https://reports.taskscape.com/api/v1/crash-reports` |
| Current endpoint address configured in the client | `https://reports.taskscape.com/api/v1/crash-reports` |
| Legacy endpoint replaced by this change | `http://reports.taskscape.com/upload.php` |

The client opens the request only with `WINHTTP_FLAG_SECURE` on the standard
HTTPS port. It does not relax certificate or hostname checks. Redirects are
disabled, so the service cannot redirect a report to HTTP. The client honours
an explicitly configured Windows/Internet Settings proxy; HTTPS remains
end-to-end between the client and `reports.taskscape.com` through a standard
CONNECT-capable proxy.

The endpoint must return a 2xx HTTP status and the established bounded result
body `<response>0</response>` for a successful upload. Non-2xx responses,
certificate failures, proxy failures, malformed responses, and redirects are
reported to the user and leave the archive on disk.

## Data in one crash report

The upload contains exactly one `.7Z` file. Its multipart filename is the
archive filename and its payload is the archive bytes. The crash reporter
creates the archive from every file matching the selected report base name
(`BaseName.*`), so the report may contain the following files:

| Data | Source | When included |
|---|---|---|
| Windows minidump (`.DMP`) | `MiniDumpWriteDump` for the crashed Open Salamander process | Created for the crash; it can contain process memory, loaded-module, thread, handle, and exception data. This can include sensitive data that happened to be in process memory. |
| Crash text report (`.TXT`) | The main application writes its crash/call-stack report after Salmon signals dump completion | Created for the crash; it identifies the crash and contains diagnostic text. |
| User report (`.INF`) | The bug reporter writes `Email: <optional contact email>` followed by the optional “Last action” description | Created only when the user presses Send Report. The contact email and description are optional, but both are included verbatim when supplied. |
| Other files with the same base name | Existing crash-report collection in the local bug-report directory | Included only if present for that report base name. |

The multipart request itself additionally exposes the archive filename, the
standard HTTPS request metadata needed to deliver it, and the destination
hostname. It does not add telemetry, cookies, an authentication token, or
separate machine identifiers. A report base name can include the locally
stored crash-reporter UID, application version, and timestamp, so that
identifier is visible in the archive filename.

## Local handling and choices

Reports are assembled beneath the current user's Local AppData Open Salamander
folder. The **View Report** button opens that folder so the user can inspect
the files before choosing to send. **Do Not Send Report** skips network
transmission and follows the dialog's deletion flow. A failed transmission
keeps the `.7Z` archive and shows its folder so it can be sent through another
channel or deleted by the user.
