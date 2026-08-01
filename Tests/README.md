# LiveIcons parser smoke tests

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

The deterministic suite creates temporary EPUB and FB2 fixtures with an
embedded 32-by-48 BMP cover. Both parsers are exercised through their path and
in-memory `IStream` entry points. The suite also checks extension routing and
the public ZIP cache traversal/rewind behavior.

Run a recursive corpus smoke test instead:

```powershell
.\Tests\bin\x64\Debug\LiveIconsTests.exe --corpus D:\Books
```

Each `.epub`, `.fb2`, `.mobi`, `.azw`, `.azw3`, and `.chm` file is parsed once
by path and once through `IStream`. Every returned `HBITMAP` is validated and
deleted. A failure produces a nonzero exit code.

Once `ParserCbr` is implemented, enable its guarded source, UnRAR project
reference, and corpus route by building with
`/p:LiveIconsEnableCbrTests=true`. Until then, `.cbr` files in a corpus are
reported as skipped rather than silently ignored.
