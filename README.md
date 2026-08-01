LiveIcons
--

The library is functional and supports generating Live Icons for EPUB, MOBI, AZW, AZW3, FB2 and CHM files.
At this moment I'm working on adding CBR and LIT formats.

Build requirements.
Visual Studio 2022, C++.

Notes.
To clone the project use the recursive flag:
git clone --recursive https://github.com/dreshetnyak/LiveIcons.git
That will also pull the submodules code.

Dependency pins, upstream sources, and the small fork-specific build
adaptations are documented in [DEPENDENCIES.md](DEPENDENCIES.md). LiveIcons
uses MiniZip's public archive-position API and does not copy zlib internals.

Parser regression tests are documented in [Tests/README.md](Tests/README.md).
Running `LiveIconsTests.exe` with no arguments creates deterministic EPUB and
FB2 fixtures; its corpus mode exercises all supported formats against a local
book collection without registering the shell extension.
