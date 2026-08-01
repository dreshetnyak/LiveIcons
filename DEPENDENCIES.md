# Dependency inventory

This file records the dependency baseline selected while reviving LiveIcons in
August 2026. Each modified dependency is maintained on a small `liveicons-*`
branch in the existing fork. The branch must contain integration/build changes
only; format-specific behavior belongs in the LiveIcons repository.

| Dependency | Previous LiveIcons pin | Selected upstream source | LiveIcons fork pin |
| --- | --- | --- | --- |
| zlib | `7bb473423c7f958d88a393c8dbe5c84c9db7a7fb` (1.2.13) | zlib 1.3.2, `da607da739fa6047df13e66a2af6b8bec7c2a498` | `liveicons-1.3.2` at `f6cc4a14179031c237b894175f1ebb2320969af1` |
| libmobi | `7f7c6a1a0725a445218892a85f8324b5d3cf9cae` (0.11) | public 0.12 head, `906274205c11944b628da1c553b255acb1af7c55` | `liveicons-0.12` at `559183f1d6d4ab627aa56511f98a390993ac3b07` |
| CHMLib | `8cad6e629cee1a5a7b769cde46e262cae9c6c055` | original repository tip `2bef8d063ec7d88a8de6fd9f0513ea42ac0fa21f` (already an ancestor) | `liveicons-0.40a` at `c3bfff838df20b0a78329fc902a7be4bbbe7c27d` |
| UnRAR | not present on `master`; unfinished CBR work used `0a0d310` (6.21) | RARLAB `unrarsrc-7.2.7` / product 7.23, mirrored by `d861246` | `liveicons-7.23` at `48e58525ae24e9fa99bc3416edc1047e4f2b68ef` |

## Preserved adaptations

### zlib

- Upstream sources are unchanged.
- `contrib/vstudio/liveicons/zlibstat.vcxproj` builds zlib as a v143 static
  library with the DLL C runtime.
- `contrib/vstudio/liveicons/minizip.vcxproj` builds the legacy MiniZip API and
  its Windows I/O adapter as a v143 static library.
- Both projects support Debug/Release and Win32/x64.
- LiveIcons now uses `unzGetFilePos64` and `unzGoToFilePos64`. The copied
  private `unz64_s` layout and `ZLibInternals.h` were removed.

### libmobi

- Upstream runtime sources and public API are unchanged.
- The VS project builds a v143 static library for Debug/Release and Win32/x64.
- The internal XML writer and encryption support remain enabled.
- zlib is referenced through the LiveIcons zlib wrapper. Obsolete NuGet and
  generated-config build logic was removed.

### CHMLib

The original CHMLib repository has not changed since 2009. The current fork
contains the later Sumatra-derived reader modernization required by
`ParserChm`, followed by the existing VS2022 build project and LiveIcons cover
heuristics. Replacing it with the original repository tip would be a
functional regression, so the runtime source is intentionally retained. The
new local commit changes only `.gitignore` so VS build output does not dirty the
submodule.

### UnRAR

- The imported source matches RARLAB's `unrarsrc-7.2.7.tar.gz` after line-ending
  normalization. The verified archive SHA-256 is
  `01d903a7dcf413cb2925696d7796e48e38d471f79bfe7ef3ad2aebf6c12dbefd`.
- Only `.gitignore` and `UnRARDll.vcxproj` differ from the source snapshot.
  The project builds a v143 static library for Debug/Release (and the upstream
  `release_nocrypt` configuration) on Win32/x64, using `/MDd` or `/MD` to
  match LiveIcons.
- None of the unfinished `FileVector`, `Archive`, private `DataSet`, or
  in-memory CBR modifications were replayed. CBR uses only the public UnRAR DLL
  API.
- Keep `unrar/license.txt` intact in source and binary distributions. UnRAR may
  be used for extraction, but its source may not be used to recreate the RAR
  compression algorithm.

## Validation baseline

- zlib and MiniZip: all four Debug/Release x Win32/x64 builds; zlib runtime
  examples; MiniZip create/extract/hash round trips on both architectures.
- libmobi: all four builds against the selected zlib wrapper.
- CHMLib: all four builds.
- UnRAR: Debug, Release, and `release_nocrypt` on Win32/x64.
- LiveIcons: `LiveIcons.dll` rebuilt from the complete solution in Debug and
  Release on Win32/x64.
- Parser regression suite: 14 deterministic checks pass in all four
  configurations, including RAR5 non-solid and solid CBR parsing by path and
  `IStream`, nonzero stream-position restoration, and malformed-CBR failure.

## Upstream references

- zlib: <https://github.com/madler/zlib/releases/tag/v1.3.2>
- libmobi: <https://github.com/bfabiszewski/libmobi/releases/tag/v0.12>
- CHMLib: <https://github.com/jedwing/CHMLib>
- UnRAR: <https://www.rarlab.com/rar_add.htm>

The `upstream` remotes are configured locally inside the zlib, libmobi, and
UnRAR repositories. The `origin` remotes and `.gitmodules` continue to point to
the LiveIcons forks. New local dependency commits must be pushed to those forks
before a fresh recursive clone can resolve the superproject pins.
