LiveIcons
--

The library supports generating Live Icons for EPUB, MOBI, AZW, AZW3, FB2,
CHM, and CBR files. LIT support remains future work.

Build requirements.
Visual Studio 2022, C++.

Versioning.
LiveIcons releases follow Semantic Versioning (`MAJOR.MINOR.PATCH`). Release
tags use `vMAJOR.MINOR.PATCH`; Windows numeric version resources map that value
to `MAJOR,MINOR,PATCH,0` while displaying the three-part semantic version.

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
individual encoded image buffers at 64 MiB. RAR dictionaries are capped at
128 MiB, and multi-volume CBR archives are rejected before UnRAR can traverse
sibling files. The shared WIC decoder rejects decoded bitmaps above 64 MiB
(about 16.8 megapixels) for every format.

CBR cover choice is independent of the order in which files were stored in the
RAR. A root-level `ComicInfo.xml` page marked `FrontCover` takes precedence,
followed by exact cover-name hints (`cover`, `front`, `frontcover`, or `folder`) and then
natural filename order. If a preferred member is corrupt or is not a usable
cover image, extraction continues with the next ranked candidate. ComicInfo
page indices are resolved against the naturally sorted image list, matching
the convention used by ComicRack.

Parser regression tests are documented in [Tests/README.md](Tests/README.md).
Running `LiveIconsTests.exe` with no arguments creates deterministic EPUB and
FB2 fixtures and decodes checked-in RAR5 CBR fixtures; its corpus mode exercises
all supported formats against a local book collection without registering the
shell extension.

The COM exception boundary, resource limits, privacy-preserving LocalLow log,
rotation policy, and native-fault limitations are documented in
[ERROR_HANDLING.md](ERROR_HANDLING.md).
