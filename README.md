LiveIcons
--

The library is functional and supports generating Live Icons for EPUB, MOBI, AZW, AZW3, FB2 extensions.
At this moment I'm working on adding CHM and then possibly CBR and LIT formats.

Build requirements.
Visual Studio 2022, C++.

Notes.
To clone the project use the recursive flag:
git clone --recursive https://github.com/dreshetnyak/LiveIcons.git
That will also pull the submodules code.

If unzip.c in ZLib is ever updated then make sure that ZLibInternals.h is up to date. It contains some internal structures copy-pasted from the ZLib.
