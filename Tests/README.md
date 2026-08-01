# LiveIcons tests

`LiveIconsTests` is a small, framework-free console program. It compiles the
production parser and support sources directly and links the same static
dependency projects as the Explorer DLL. It does not register or load the
shell extension.

Build from a Visual Studio developer prompt (or with the full path to
`MSBuild.exe`):

```powershell
msbuild .\Tests\LiveIconsTests.vcxproj /m /p:Configuration=Debug /p:Platform=x64
```

Run deterministic tests (the default):

```powershell
.\Tests\bin\x64\Debug\LiveIconsTests.exe
```

The 36-test deterministic suite creates temporary EPUB and FB2 fixtures with
an embedded 32-by-48 BMP cover. It also decodes checked-in RAR5 fixtures into
temporary CBR files. These cover non-solid and solid extraction, scrambled
physical member order, natural numeric filename ordering, a corrupt preferred
candidate followed by a valid fallback, an oversized preferred-image fallback,
and a `ComicInfo.xml` `FrontCover` override in a solid archive. Each successful
CBR extraction and selection scenario is exercised through both path and
in-memory `IStream` entry points. The suite also checks the pure selection
policy, extension routing, bitmap ownership, the public ZIP cache
traversal/rewind behavior, archive-path normalization, input budgets, malformed
EPUB/FB2/MOBI/CHM/CBR data, and adversarial `IStream` implementations that
return short reads, over-report bytes, or throw. Stream position restoration is
verified after both success and failure. The retained CHMLib fork is also
exercised with a valid compressed CHM and in-memory cyclic directory/empty-path
mutations. Focused libmobi checks reject zero-length HUFF codes in a
timeout-isolated child process and reject hostile CDIC allocations before
reserving memory. Deterministic RAR5 header mutations verify the 128 MiB
dictionary limit and reject multi-volume traversal.

`LiveIconsBoundaryTests` is built beside `LiveIcons.dll`. It loads the actual
DLL exports and checks COM output contracts, object/server-lock lifetime,
concurrent reference counting, C++ exception translation, log privacy, and
rate limiting:

```powershell
.\bin\x64\Debug\LiveIconsBoundaryTests.exe
```

Run a recursive corpus smoke test instead:

```powershell
.\Tests\bin\x64\Debug\LiveIconsTests.exe --corpus D:\Books
```

Each `.epub`, `.fb2`, `.mobi`, `.azw`, `.azw3`, `.chm`, and `.cbr` file is
parsed once by path and once through `IStream`. Every returned `HBITMAP` is
validated and deleted. A failure produces a nonzero exit code.

CBR parser integration tests and the UnRAR project reference are enabled by
default. They can be intentionally omitted with
`/p:LiveIconsEnableCbrTests=false`; the pure cover-selection policy check still
runs, while `.cbr` files in a corpus are reported as skipped rather than
silently ignored.
