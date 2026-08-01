# Error handling and diagnostics

LiveIcons treats every exported DLL function and COM method as a trust
boundary. A failed thumbnail request must return an `HRESULT`, leave its output
bitmap null, and leave its alpha type as `WTSAT_UNKNOWN`. C++ exceptions are
not allowed to cross the COM ABI:

- `std::bad_alloc` becomes `E_OUTOFMEMORY`.
- Other C++ exceptions become `E_UNEXPECTED`.
- An `HRESULT` returned by Windows, a parser, or an input `IStream` is preserved
  whenever possible.
- COM objects, streams, registry keys, file handles, archive handles, and
  `HBITMAP` values use scoped ownership so failed operations release partially
  acquired resources.
- MiniZip, CHMLib, and UnRAR callbacks are `noexcept` firewalls because a C++
  exception crossing a C callback ABI would terminate the host process.

## Diagnostic log

Error and exception records are written to:

```text
%USERPROFILE%\AppData\LocalLow\LiveIcons\Logs\LiveIcons-<process-id>.log
```

The implementation resolves `FOLDERID_LocalAppDataLow` rather than constructing
that path, so redirected profiles continue to work. It falls back to
`%LOCALAPPDATA%` only when the known folder cannot be resolved. `LocalLow` is
used because Windows normally hosts stream-based thumbnail providers in an
isolated, low-integrity process.

Each record contains a UTC timestamp, severity, stable event name, `HRESULT`,
request correlation ID, process ID, thread ID, architecture, and a short static
failure category. It deliberately does **not** contain document paths, archive
member names, file content, or `std::exception::what()` text.

Logging is best effort and cannot change a thumbnail result:

- Logger entry points are `noexcept` and preserve the caller's `GetLastError()`.
- Contended logger calls are dropped instead of waiting behind another
  thumbnail request's filesystem operation.
- File and formatting failures fall back to `OutputDebugString`.
- Error and exception output is limited to 120 records per rolling 60 seconds;
  one marker records that further output is being suppressed.
- The active log rotates at 2 MiB and keeps three rotations (`.1` through
  `.3`).
- On first use, cleanup makes a best-effort pass over at most 256 matching logs
  and removes ordinary files older than 14 days.
- Diagnostic success/unsupported records are emitted only by Debug builds.
  Error and exception records are emitted in both Debug and Release builds.

## Input and resource limits

Malformed input is rejected before large allocations where the format API
allows it. Current limits include:

The shared WIC adapter presents encoded bytes through a read-only borrowed
`IStream`, avoiding a second encoded-image allocation of up to 64 MiB while the
source buffer remains alive for the synchronous decode.

| Area | Limit |
| --- | ---: |
| Generic whole-file or whole-stream read | 512 MiB |
| Encoded image / decoded WIC bitmap | 64 MiB / 64 MiB |
| ZIP entries | 10,000 |
| ZIP member | 64 MiB |
| ZIP member path / aggregate cached paths | 4 KiB / 8 MiB |
| CHM entries / individual entry | 10,000 / 64 MiB |
| MOBI markup / image resource / HUFF-CDIC offsets | 64 MiB / 64 MiB / 2 MiB |
| CBR entries / retained image-name characters | 10,000 / 2 million |
| CBR image / ComicInfo metadata / ComicInfo page tags | 64 MiB / 1 MiB / 10,000 |
| CBR total decompression / metadata discovery / RAR dictionary | 512 MiB / 64 MiB / 128 MiB |
| CBR `IStream` snapshot | 1 GiB |

## Native-fault boundary

The C++ exception firewall is intentionally compiled with standard C++
exception semantics. It cannot safely recover from access violations, stack
overflow, heap corruption, fail-fast termination, or a fatal defect inside a
native codec. Attempting to continue after those failures could corrupt the
Explorer host, and the logger itself cannot be considered safe in that state.

Keep Explorer's default thumbnail process isolation enabled; do not ship a
`DisableProcessIsolation` registry value. For a fatal native failure, use
Windows Error Reporting or a crash dump together with the last process log.
Microsoft's guidance explains both the isolation model and why
`IInitializeWithStream` is preferred:

- <https://learn.microsoft.com/windows/win32/shell/thumbnail-providers>
- <https://learn.microsoft.com/windows/win32/shell/building-thumbnail-providers>

## Verification

`LiveIconsTests` exercises parsers, malformed data, size limits, short and
throwing `IStream` implementations, position restoration, ownership, and
logger `noexcept` behavior. `LiveIconsBoundaryTests` dynamically loads the DLL
and verifies COM output contracts, module lifetime, concurrent reference
counting, exception-to-`HRESULT` mapping, log privacy, and rate limiting.

Build and run both projects for Debug and Release on Win32 and x64 before a
release. An Explorer-host smoke test remains necessary because a unit-test
process cannot reproduce every shell-host policy or installed codec.
