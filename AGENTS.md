# LiveIcons agent guide

## Scope and mission

This file applies to the whole `LiveIcons` checkout, including the four pinned
submodule working trees. LiveIcons is a Windows in-process COM thumbnail
provider. It extracts embedded cover art for EPUB, FB2, MOBI, AZW, AZW3, CHM,
and CBR files and returns an `HBITMAP` to Windows Explorer. LIT remains future
work; CBZ is not currently routed.

Reliability is more important than producing a thumbnail. A malformed,
unsupported, encrypted, or excessively expensive input must fail with a useful
`HRESULT` and a null bitmap; it must not crash or stall Explorer. Operational
failures are logged when they reach the COM boundary. Direct parser calls do
not log, and unsupported routing produces a diagnostic only in Debug builds.

The user's design preferences are Clean Code and SOLID. Keep responsibilities
cohesive, use explicit ownership and small named helpers, preserve separation
between selection policy and archive mechanics, and avoid speculative
abstractions or feature work in third-party forks.

## Canonical checkout and history

- Work in this repository on branch `wip`. It was deliberately created from
  `origin/master` at `79fad18`; there is no local `master` branch.
- `develop` contains old unfinished CBR/UnRAR experiments. Treat it as
  archaeological reference only. Do not merge or rebase it wholesale, and do
  not revive its private UnRAR `FileVector`, `Archive`, or `DataSet` approach.
  The abandoned history and `wip` diverged at `402e0af`.
- Sibling copies such as `C:\Projects\LiveIcons_Local`,
  `C:\Projects\LiveIcons_`, or `C:\Projects\LiveIcons_Fubar` are not canonical.
  Do not copy changes from them unless the user explicitly asks for a
  historical comparison.
- The current `wip` branch has no configured upstream and is not advertised by
  `origin`. Do not claim the work is remotely backed up, and do not push or
  create remote branches without the user's authorization.
- Always inspect `git status --short --branch` and `git submodule status` before
  editing. Preserve unrelated changes and user-owned files.
- `data-samples/` is an untracked, non-ignored, user-owned corpus containing
  large book/comic files. Never edit, delete, stage, or commit it. Avoid
  `git add -A`; stage explicit project-owned paths.
- `.vs/`, build `bin/` and `obj/` trees are generated. In contrast,
  `LiveIcons.sln.DotSettings.user`, `LiveIcons/LiveIcons.vcxproj.user`, and
  `LiveIcons/Testing.cpp` are tracked legacy files; do not delete them as
  presumed build output. `Testing.cpp` is not the active test harness and is
  not compiled by `LiveIcons.vcxproj`.
- Avoid bulk formatting and line-ending normalization. This Windows checkout
  uses `core.autocrlf=true`, and harmless LF-to-CRLF warnings can appear in Git.

## Read these sources of truth

Before changing the corresponding area, read:

- `README.md` for supported formats and current product behavior.
- `DEPENDENCIES.md` for exact dependency pins, upstream baselines, fork
  adaptations, licensing, and validation history.
- `ERROR_HANDLING.md` for COM exception boundaries, diagnostics, privacy, and
  input/resource limits.
- `Tests/README.md` for build and test modes.
- `Tests/Fixtures/README.md` before changing checked-in archive fixtures.
- `Changes.txt` for release-facing behavior already promised.

Update those documents when code changes make them inaccurate. Prefer linking
to the detailed documents instead of creating a second conflicting inventory.

## Toolchain and build

The product is Windows-only and uses MSBuild/Visual C++ with the v143 toolset,
Windows 10 SDK, C++20 for project code, and C17 where C sources are compiled.
Use a Visual Studio Developer PowerShell. If `msbuild.exe` is not on `PATH`,
locate it with Visual Studio Installer's `vswhere.exe`; do not hard-code a
particular installed Visual Studio edition or version directory.

The nominal command for cloning a published revision with its submodules is:

```powershell
git clone --recursive https://github.com/dreshetnyak/LiveIcons.git
```

This currently selects published `master`, not local `wip`. A recursive clone
still depends on every legacy gitlink being reachable; verify the required
pins rather than promising recovery. The same command is suitable for `wip`
only after its root and dependency refs have been published. If a clone was
created without recursion,
`git submodule update --init --recursive` is the normal initialization command,
but only when every required pin is known to be reachable.

No configured fork `origin` ref currently advertises the dependency pins used
by `wip`. Do not assume this exact state is fetchable, and do not discard or
replace this valuable working checkout in an attempt to test clone recovery;
see "Dependency and submodule policy" below. In this unpublished checkout, do
not run `git submodule update` unless root and submodule status are clean, exact
pin reachability is verified, and moving the submodule checkouts away from
their local integration branches is explicitly intended.

Build the complete solution:

```powershell
msbuild .\LiveIcons.sln /m /p:Configuration=Debug /p:Platform=x64
msbuild .\LiveIcons.sln /m /p:Configuration=Release /p:Platform=x64
msbuild .\LiveIcons.sln /m /p:Configuration=Debug /p:Platform=x86
msbuild .\LiveIcons.sln /m /p:Configuration=Release /p:Platform=x86
```

At solution level the 32-bit platform is named `x86`; it maps to project
platform and output folder `Win32`. A direct `.vcxproj` build must use
`/p:Platform=Win32`, not `x86`.

Outputs are:

- DLL and boundary tests: `bin/<x64|Win32>/<Debug|Release>/`
- Parser tests: `Tests/bin/<x64|Win32>/<Debug|Release>/`

When adding/removing a source file, update the relevant `.vcxproj` and
`.vcxproj.filters`. `Tests/LiveIconsTests.vcxproj` compiles production parser
and helper sources directly, so a new production translation unit may also
need to be added there. A successful DLL build alone does not prove that the
test project is correctly wired.

The dependency libraries and LiveIcons must continue to use compatible DLL
C runtimes (`/MDd` for Debug and `/MD` for Release). There is no NuGet restore.
XmlLite, WIC, COM, GDI, and shell APIs come from Windows/its SDK. WinRAR is not
needed to build or run checked-in tests; it is needed only to regenerate RAR
fixtures.

## Architecture map

| Area | Main files | Responsibility |
| --- | --- | --- |
| COM DLL/export boundary | `DllMain.cpp`, `ClassFactory.*`, `LiveIcons.*` | Activation, lifetime, `IInitializeWithStream`, `IThumbnailProvider`, parser routing |
| Registration | `Configuration.*`, `Registry.*` | Per-user CLSID and extension associations |
| Parser contract | `ParserBase.*` | Move-only result and `HBITMAP` ownership |
| Format parsers | `ParserEpub.*`, `ParserFb2.*`, `ParserMobi.*`, `ParserChm.*`, `ParserCbr.*` | Format-specific cover discovery and extraction |
| CBR policy | `CbrCoverSelection.*` | Natural ordering, explicit-name hints, bounded ComicInfo parsing |
| Image decode | `Gfx.*` | WIC decode, encoded/decoded size checks, cover geometry |
| ZIP support | `ZipArchive.*`, `ZipCache.*`, `ZipStream.*`, `ZipPosition.h` | Public MiniZip traversal and stream adaptation |
| General I/O | `Utility.*`, `RamFile.*`, `DataIStream.*`, `GlobalMem.*` | Checked file/stream reads and scoped temporary storage |
| Diagnostics | `ExceptionBoundary.h`, `Log.*` | Exception-to-`HRESULT` translation and privacy-safe logging |
| Tests | `Tests/LiveIconsTests.cpp`, `Tests/LiveIconsBoundaryTests.cpp` | Parser/hardening tests and actual-DLL COM boundary tests |

`LiveIcons.cpp` routes EPUB to `Parser::Epub`, FB2 to `Parser::Fb2`, MOBI/AZW/
AZW3 to `Parser::Mobi`, CHM to `Parser::Chm`, and CBR to `Parser::Cbr`.
Explorer invokes the stream entry point; path entry points remain important for
tests, tools, and corpus validation.

COM routing first obtains the initialized stream's filename through
`IStream::Stat`/`Utility::GetIStreamFileName`, then routes by its extension. A
nameless stream fails before parser dispatch. Stream mocks and new initialization
paths must provide and preserve that metadata contract.

## Parser and ownership contracts

- `CanParse` must remain case-insensitive and limited to the parser's declared
  extensions.
- On success, return `Parser::Result` with a successful `HResult` and non-null
  cover. On failure, return a failing `HRESULT` and no owned bitmap.
- `Parser::Result` is move-only and owns its `HBITMAP`; its destructor calls
  `DeleteObject`. Call `ReleaseCover()` only at an explicit ownership handoff.
- Initialize COM output parameters before any fallible work. On every
  `GetThumbnail` failure, the bitmap must be null and alpha must be
  `WTSAT_UNKNOWN`.
- Preserve meaningful Windows, parser, or caller-`IStream` `HRESULT`s whenever
  possible. Preserve `E_OUTOFMEMORY` rather than collapsing it into a generic
  decode error.
- Preserve the caller's `IStream` position after both success and failure.
  Assume streams may seek oddly, short-read, over-report bytes, or throw.
- Decode image data through `Gfx::LoadImageToHBitmap`; do not bypass the shared
  encoded/decoded allocation checks. Adopt a returned bitmap immediately into
  scoped ownership so later allocations cannot leak it.
- A cover must be larger than 20 pixels in both dimensions and cannot be an
  extreme landscape image (`width >= 2 * height`). Preferred candidates that
  decode but fail these constraints should normally fall through to the next
  candidate.
- `IThumbnailProvider::GetThumbnail` currently ignores requested `cx` and
  returns the decoded cover at its source dimensions. Do not assume resizing
  exists without implementing and testing it deliberately.

## Exception safety and diagnostics

No C++ exception may cross a DLL export, COM ABI, or C callback ABI.

- Wrap exported and COM work with `ExceptionBoundary::ToHResult`:
  `std::bad_alloc` becomes `E_OUTOFMEMORY`; other C++ exceptions become
  `E_UNEXPECTED`.
- MiniZip, CHMLib, and UnRAR callbacks must remain `noexcept` firewalls and
  catch/translate internally. An exception escaping a C callback can terminate
  the Explorer host.
- Use RAII for COM pointers, streams, Win32 handles, registry keys, archive
  handles, temporary files, buffers, and `HBITMAP`s. Destructors used on failure
  paths must be `noexcept`.
- Logging is best effort and may never alter a thumbnail result or output
  contract. Do not replace a meaningful operation failure with a logging
  failure.
- The C++ firewall cannot safely recover from access violations, stack
  overflow, heap corruption, fail-fast termination, or fatal native-code
  defects. Do not add structured-exception swallowing. Keep Explorer's default
  thumbnail-provider process isolation enabled; never register
  `DisableProcessIsolation`.

Logs normally live at:

```text
%USERPROFILE%\AppData\LocalLow\LiveIcons\Logs\LiveIcons-<process-id>.log
```

The logger resolves `FOLDERID_LocalAppDataLow` and falls back to
`%LOCALAPPDATA%` only when necessary. Calls are `noexcept`, preserve
`GetLastError`, avoid waiting on a contended logger lock, rate-limit output,
rotate at 2 MiB, retain three rotations, and remove ordinary matching logs
older than 14 days on a bounded best-effort pass.

Never log document paths, archive member names, document content,
`std::exception::what()`, passwords, or other user data. Use stable event IDs,
the `HRESULT`, correlation ID, and a short static category. Debug builds may
emit success/unsupported diagnostics; errors and exceptions are logged in both
Debug and Release.

## Current resource limits

Treat these as security and availability invariants. Check arithmetic and
sizes before allocating. Do not weaken a limit without an explicit rationale,
targeted adversarial tests, and matching `ERROR_HANDLING.md` updates.

| Area | Limit |
| --- | ---: |
| Generic whole-file/whole-stream read | 512 MiB |
| WIC encoded image / decoded bitmap | 64 MiB / 64 MiB |
| ZIP entry count / member | 10,000 / 64 MiB |
| ZIP member path / aggregate cached paths | 4 KiB / 8 MiB |
| CHM entry count / entry / input | 10,000 / 64 MiB / 512 MiB |
| MOBI markup or image resource / HUFF-CDIC offsets | 64 MiB / 2 MiB |
| CBR entries / retained filename characters | 10,000 / 2 million |
| CBR image / ComicInfo / ComicInfo page tags | 64 MiB / 1 MiB / 10,000 |
| CBR metadata discovery / cumulative decompression | 64 MiB / 512 MiB declared and 512 MiB actual |
| RAR dictionary / CBR `IStream` snapshot | 128 MiB / 1 GiB |

The WIC adapter presents encoded bytes through a borrowed read-only `IStream`
to avoid a second copy. Its source buffer must remain alive for the complete
synchronous decode; do not retain that stream beyond the call.

## CBR behavior and invariants

CBR support uses only UnRAR's public DLL API and is intentionally split between
policy (`CbrCoverSelection.*`) and archive I/O (`ParserCbr.cpp`). A process-wide
mutex serializes CBR parsing because the statically linked UnRAR library has
process-global error state.

The algorithm is deliberately independent of physical RAR member order:

1. Catalog archive headers and naturally sort the image-only list.
2. If there is exactly one non-directory, non-redirection, root-level
   `ComicInfo.xml`, parse it with bounded XmlLite settings. `Page Image`
   indices address the naturally sorted image-only list, not physical headers
   or arbitrary archive entries.
3. Rank valid `FrontCover` page indices first, in metadata order.
4. Rank exact normalized basename hints `cover`, `front`, `frontcover`, and
   `folder` next.
5. Rank remaining candidates in natural filename order.
6. Reopen the archive and validate catalog fingerprints while scanning headers
   in physical order. For non-solid archives, preselect the bounded attempt set
   in logical rank order, retain the best successfully decoded logical rank,
   and stop once no later physical entry can improve it. Solid archives process
   required physical prefixes. If a preferred image is corrupt, unsupported,
   undersized, extreme-landscape, or statically oversized, continue toward the
   next usable candidate. Decode out-of-memory remains terminal.

Natural comparison is case-insensitive and numeric-aware, normalizes archive
path separators, and sorts the more zero-padded spelling first when numeric
values are equal. Deterministic ties use exact code-unit names and entry
fingerprints; physical index is only a final discriminator for otherwise
indistinguishable duplicates.

ComicInfo rules:

- `ComicInfo` is the document root; `Pages` must be its direct child and
  `Page` a direct child of `Pages`.
- `Type` may contain a case-insensitive `FrontCover` token.
- DTDs are prohibited. XML depth is capped at 64, entity expansion at 1 MiB,
  and page tags at 10,000.
- Duplicate, malformed, over-depth/entity/page-count, oversized, encrypted,
  excessive-dictionary, or too-expensive-to-discover metadata is generally
  ignored in favor of filename ranking. Out-of-memory is terminal. If metadata
  extraction was selected and then exhausts the shared decompression-work
  budget, that work-limit failure is also terminal.

Ignore directories, redirections, `__MACOSX` trees, and `._` sidecars.
Candidate extensions currently include BMP/DIB, GIF, HEIC/HEIF, ICO, JPEG
variants, JXR/WDP, PNG, TIFF, WebP, AVIF, and JPEG 2000 variants. An extension
only makes a member a candidate; actual decode depends on WIC support.

For non-solid archives, reserve extraction work in logical rank order so
physical member order cannot spend the budget on inferior pages. Solid archives
must process required physical prefixes. If a valid earlier fallback has
already been decoded, a later encrypted, excessive-dictionary, or work-limit
barrier may return that fallback. Multi-volume and encrypted CBRs are not
supported; never prompt for or retain passwords.

ComicInfo discovery and cover extraction share one work budget. The 512 MiB
limit is enforced independently against cumulative declared bytes and
cumulative actual bytes received from UnRAR; neither counter may exceed it.

UnRAR reopens inputs by path. The CBR `IStream` overload therefore creates a
uniquely named bounded temporary snapshot. Restore the incoming stream position
and remove the snapshot on every path. Keep the two catalog/extraction passes'
name, size, flags, checksum/hash, dictionary, and redirection fingerprint checks
so a changed archive cannot silently invalidate ranking assumptions.

## Dependency and submodule policy

The four submodules point to the user's minimal forks. Exact upstream commits,
fork pins, patches, and licenses are authoritative in `DEPENDENCIES.md`.

| Submodule | Local integration branch | Current pin |
| --- | --- | --- |
| `zlib` | `liveicons-1.3.2` | `f6cc4a14179031c237b894175f1ebb2320969af1` |
| `libmobi` | `liveicons-0.12` | `2416e40c3c942d5ea137fc8d5920e2127307c1e6` |
| `chmlib` | `liveicons-0.40a` | `df46c6290253861bca82f4e2b19b75954a19db44` |
| `unrar` | `liveicons-7.23` | `48e58525ae24e9fa99bc3416edc1047e4f2b68ef` |

As observed on 2026-08-01, no configured fork `origin` ref advertises any of
these four pins or their `liveicons-*` branch names, none of the local branches
tracks a remote branch, and `origin` does not advertise root branch `wip`. Do
not assume these commits are fetchable from a fresh clone. `origin/master`
predates the new pins, but reachability of all of its legacy gitlinks must also
be checked before promising a successful recursive clone. Publishing `wip`
without first publishing its dependency refs can make its recursive checkout
fail. Do not deinitialize, reset, clean, replace, or garbage-collect these
submodule working trees.

Before publishing a superproject pointer, push and verify the submodule commit
first, then push the root branch. `git ls-remote` can succeed with no matching
branch, so require the exact remote ref and compare its hash with the gitlink.
For example:

```powershell
$localPin = git -C zlib rev-parse HEAD
$remoteRef = git -C zlib ls-remote --exit-code origin refs/heads/liveicons-1.3.2
if ($LASTEXITCODE -ne 0 -or ($remoteRef -split '\s+')[0] -ne $localPin) {
  throw 'zlib integration pin is not advertised by the expected origin ref'
}
```

Repeat that exact-ref/hash check for every changed dependency. Once published,
never rewrite or force-push an integration branch: older superproject commits
continue to depend on its old gitlink objects. Make dependency upgrades through
additive commits or a new versioned `liveicons-*` branch and keep earlier pins
reachable.

Dependency rules:

- Do not run `git submodule update --remote` or blindly move a pin to an
  upstream tip.
- Product/format features belong in LiveIcons. Fork changes are restricted to
  minimal build integration and unavoidable malformed-input safety fixes.
- Keep zlib upstream runtime sources unchanged. Use public MiniZip
  `unzGetFilePos64`/`unzGoToFilePos64`; never restore copied private zlib
  layouts.
- Keep libmobi's public API unchanged and its LiveIcons changes limited to the
  documented malformed-input guards/build integration.
- CHMLib's Sumatra-derived modernization and LiveIcons heuristics are required.
  Replacing them with the old original upstream tip is a functional regression.
- Use only UnRAR's public extraction API. Preserve `unrar/license.txt` and the
  required UnRAR notice already embedded at the top of `ParserCbr.cpp` and in
  distribution documentation. UnRAR source may not be used to recreate the
  proprietary RAR compression algorithm.
- `upstream` remotes are local submodule configuration and do not propagate in
  `.gitmodules`. Configured `origin` remotes point to the user's forks.
- If a dependency change is authorized: inspect its status, work on the proper
  `liveicons-*` branch, keep the patch narrow, build/test it, commit it inside
  the submodule, obtain authorization to push it, verify exact remote
  reachability, and only then stage the superproject gitlink and update
  `DEPENDENCIES.md`. A fresh `git submodule update` normally leaves a detached
  HEAD; explicitly create or switch to the intended local integration branch
  before committing dependency work. Never create new work on detached HEAD.

## Tests and verification

`Tests/LiveIconsTests.cpp` is a framework-free parser and hardening suite. A
default run currently has 36 deterministic tests. It builds fixtures, decodes
checked-in RAR5 fixture text, checks success/failure ownership, exercises both
path and `IStream` parsing, verifies stream restoration, and attacks parsers
with malformed and adversarial inputs.

`Tests/LiveIconsBoundaryTests.cpp` loads the actual built DLL. It currently has
seven tests covering exports, COM output/lifetime/server-lock contracts,
concurrent reference counting, exception translation, and log privacy/rate
limiting. Treat counts as the 2026-08-01 baseline and update documentation when
adding tests.

Run a deterministic pair for one configuration:

```powershell
.\Tests\bin\x64\Debug\LiveIconsTests.exe
.\bin\x64\Debug\LiveIconsBoundaryTests.exe
```

Run corpus-only or deterministic-plus-corpus modes:

```powershell
.\Tests\bin\x64\Debug\LiveIconsTests.exe --corpus .\data-samples
.\Tests\bin\x64\Debug\LiveIconsTests.exe --all .\data-samples
```

Corpus mode recursively parses every recognized file through both path and
`IStream`; it must not modify the corpus. No arguments and `--self-test` both
run deterministic tests. A failure returns a nonzero exit code.

CBR integration is enabled by default. The explicit build property
`/p:LiveIconsEnableCbrTests=false` omits ParserCbr/UnRAR integration while
retaining pure cover-selection policy tests; do not use it for normal release
validation.

Test expectations:

- Add a regression test for every fixed bug and every new selection rule.
- Exercise successful parser behavior by both path and `IStream` where both
  entry points exist.
- Test malformed/truncated input, checked arithmetic, limits, ownership,
  failure output state, stream restoration, and ranked decode fallback as
  appropriate.
- Verify which cover was chosen through deterministic dimensions/content, not
  merely that some bitmap was returned.
- CBR fixtures belong as Base64 text under `Tests/Fixtures`. Document purpose,
  physical member order, decoded length, SHA-256, and reproducible generation
  details in `Tests/Fixtures/README.md`.
- Never turn a personal corpus file into a committed fixture without the
  user's explicit permission and a copyright/privacy review.

For parser, COM, dependency, resource-limit, or release-level changes, build
and run both suites in Debug and Release on x64 and Win32. Run project-owned
static analysis and distinguish existing upstream dependency warnings from new
LiveIcons warnings. Finish release-level work with an Explorer-host smoke test;
a unit process cannot reproduce every shell-host policy or installed WIC codec.

## Registration, deployment, and Explorer caching

The handler CLSID is `{434647DD-455C-4F43-AA12-6EFD055F5B08}`. Registration is
per-user under `HKCU\Software\Classes`, uses `ThreadingModel=Apartment`, and
associates the seven supported extensions. Building does not deploy or register
the DLL.

Never assume Explorer is loading the DLL just built. Query the recorded path:

```powershell
$key = Get-Item -LiteralPath `
  'Registry::HKEY_CURRENT_USER\Software\Classes\CLSID\{434647DD-455C-4F43-AA12-6EFD055F5B08}\InProcServer32'
$key.GetValue('')
```

Use architecture-matching `regsvr32` and an explicit absolute DLL path. Normal
64-bit Explorer needs the x64 DLL:

```powershell
& "$env:SystemRoot\System32\regsvr32.exe" 'C:\absolute\path\LiveIcons.dll'
& "$env:SystemRoot\System32\regsvr32.exe" /u 'C:\absolute\path\LiveIcons.dll'
```

Use `SysWOW64\regsvr32.exe` only for a Win32 DLL. Registration writes HKCU, not
machine-wide HKLM. Root `register.cmd` and `remove.cmd` refer only to relative
`LiveIcons.dll` and do not change directory; running them from the repository
root will not select a configuration under `bin`. Prefer an explicit path and
check the result.

Registration, replacing a deployed binary, unloading a previously loaded COM
DLL, and rebuilding thumbnail cache entries are separate operations. Explorer
may show a cached cover even after correct registration. When deployment
testing is explicitly requested:

1. Verify registered path, architecture, and deployed binary hash/time.
2. Unregister/register the intended absolute path as needed.
3. Restart the relevant shell/isolated thumbnail host in a user-visible,
   recoverable way.
4. Invalidate only the current user's thumbnail cache, not the unrelated icon
   cache. Prefer moving `%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db`
   to a dated backup while Explorer is stopped, then restart Explorer and let
   Windows recreate it.
5. If behavior remains wrong, inspect LocalLow logs and confirm which DLL path
   is loaded before changing parser code.

Do not mutate registration, stop Explorer/COM hosts, replace deployed binaries,
or clear caches unless the user requested deployment or shell-host testing.

## Change and handoff checklist

For ordinary code changes:

1. Inspect root and submodule status; identify user-owned changes.
2. Read the relevant design/error/dependency/test documentation.
3. Make the smallest cohesive change; preserve public APIs and ownership
   contracts unless the task explicitly requires changing them.
4. Add focused regression coverage and project/filter entries as needed.
5. Build and test proportionally to risk; use the full four-configuration
   matrix for parser, COM, dependency, or release-level changes.
6. Run `git diff --check`, inspect the complete diff, and confirm no temporary
   probes, paths, content, binaries, logs, or corpus files are included.
7. Stage explicit paths only. A changed submodule entry must be intentional and
   its commit must be safely reachable before publication.
8. Report the exact configurations/tests run, remaining caveats, registration
   or cache state changed, and any user files deliberately left untracked.

Do not weaken tests, suppress new project-owned warnings, hide failures behind
logging, or report completion solely because the code compiles.
