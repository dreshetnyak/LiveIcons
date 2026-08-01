LiveIcons
--

The library supports generating Live Icons for EPUB, MOBI, AZW, AZW3, FB2,
CHM, and CBR files. LIT support remains future work.

Build requirements.
Visual Studio 2022, C++.

Notes.
To clone the project use the recursive flag:
git clone --recursive https://github.com/dreshetnyak/LiveIcons.git
That will also pull the submodules code.

Dependency pins, upstream sources, and the small fork-specific build
adaptations are documented in [DEPENDENCIES.md](DEPENDENCIES.md). LiveIcons
uses MiniZip's public archive-position API and does not copy zlib internals.

CBR extraction uses only UnRAR's public API. To keep malformed files from
turning Explorer thumbnailing into unbounded work, stream snapshots are capped
at 1 GiB, archive scans at 10,000 entries and 512 MiB of decompressed data, and
individual encoded image buffers at 64 MiB. The shared WIC decoder rejects
decoded bitmaps above 64 MiB (about 16.8 megapixels) for every format. Multipart
CBR discovery is available to the path parser; the Explorer `IStream` path
snapshots only the provided volume and cannot discover sibling volumes.

Parser regression tests are documented in [Tests/README.md](Tests/README.md).
Running `LiveIconsTests.exe` with no arguments creates deterministic EPUB and
FB2 fixtures and decodes checked-in RAR5 CBR fixtures; its corpus mode exercises
all supported formats against a local book collection without registering the
shell extension.
