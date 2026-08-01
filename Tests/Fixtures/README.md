# Regression fixtures

## CHM fixture

The test project links `zlib/contrib/dotzlib/DotZLib.chm` into its output
fixture directory. This existing zlib documentation file verifies a valid
CHMLib directory and compressed-entry read. In-memory mutations of its PMGL
directory verify bounded cyclic-page rejection and empty-path rejection; the
source file is never modified.

## Non-solid RAR5 archive

`cbr-rar5-first-image.rar.base64` is a text-safe encoding of a 7,444-byte RAR5 archive. It is intended to verify that the CBR thumbnail parser skips a non-image member and selects the first image member.

The test suite also makes checksum-correct in-memory mutations of this fixture
to exercise oversized-dictionary and multi-volume rejection without retaining
additional archives.

Decoded archive SHA-256:

```text
ed4fcd7c9a1490933ec3c4f1a680649302b020757462b4290e4b499eb2086209
```

Archive members, in order:

1. `00-note.txt` — 101 bytes, UTF-8 without a BOM. Its two CRLF-terminated lines say that it is not cover art and that the parser must use `10-cover.bmp`. SHA-256: `a94f5e0c1a2f2f51bbefd7adb7c46b3143141cbdd4a0d576503341b5f0acb1d7`.
2. `10-cover.bmp` — an original deterministic 40x60-pixel portrait-oriented cover graphic, Windows BMP, 24-bit BGR, bottom-up, uncompressed `BI_RGB`, 7,254 bytes. It uses a dark border/background, a blue page, a coral portrait glyph, and a gold title bar. SHA-256: `2cc0847b1687ad6ef41d6959e0dc829abc3546d1b4e336f65d9823320c054e60`.

The inputs were generated twice in separate clean temporary directories with identical fixed filesystem timestamps. WinRAR 7.22 x64 created each archive with this command, and both resulting SHA-256 values matched:

```powershell
& "C:\Program Files\WinRAR\Rar.exe" a -ma5 -m0 -s- -qo- -htc -ep -ai -tsm- -tsc- -tsa- -tsp- -cfg- -idq -y cbr-rar5-first-image.rar 00-note.txt 10-cover.bmp
```

The options select RAR5, store mode, a non-solid archive, no quick-open data, CRC32 member checksums, no paths/attributes/timestamps, and no local WinRAR configuration. `Rar.exe lt` identifies the result as `RAR 5` and reports both members as `RAR 5.0(v50) -m0`.

Decode and validate:

```powershell
$text = Get-Content -Raw .\cbr-rar5-first-image.rar.base64
$bytes = [Convert]::FromBase64String($text)
[IO.File]::WriteAllBytes(".\cbr-rar5-first-image.rar", $bytes)
Get-FileHash -Algorithm SHA256 .\cbr-rar5-first-image.rar
& "C:\Program Files\WinRAR\Rar.exe" t -cfg- -c- .\cbr-rar5-first-image.rar
```

## Solid RAR5 archive

`cbr-rar5-solid-first-image.rar.base64` encodes a 1,332-byte solid RAR5 archive. It exercises the case where decoding the desired image depends on first processing a non-image member in the same solid stream.

Decoded archive SHA-256:

```text
012c8700520885012f3a73e53e275ebcd083d1dac3ae3600568aba88c5b6c7a0
```

It contains exactly the same member bytes and order as the non-solid fixture:

1. `00-note.txt` — 101 bytes.
2. `10-cover.bmp` — 7,254 bytes.

The non-solid fixture was decoded and extracted into two separate clean temporary directories. The extracted inputs were assigned the same fixed timestamps, then WinRAR 7.22 x64 created each archive with:

```powershell
& "C:\Program Files\WinRAR\Rar.exe" a -ma5 -s -m3 -md1m -mt1 -qo- -htc -ep -ai -tsm- -tsc- -tsa- -tsp- -cfg- -idq -y cbr-rar5-solid-first-image.rar 00-note.txt 10-cover.bmp
```

Both generated archives had the SHA-256 above. Single-threaded normal compression and a fixed 1 MiB dictionary make the compressed output reproducible; quick-open data, paths, attributes, timestamps, and local WinRAR configuration are disabled. `Rar.exe lt` reports `Details: RAR 5, solid`, preserves the documented member order, and marks `10-cover.bmp` as `solid`. `Rar.exe t` reports `All OK`.

RAR4 was preferred for additional format coverage, but this installed WinRAR version rejects `-ma4` with `Unknown option: ma4`; it can only create RAR5 archives.

Decode and validate:

```powershell
$text = Get-Content -Raw .\cbr-rar5-solid-first-image.rar.base64
$bytes = [Convert]::FromBase64String($text)
[IO.File]::WriteAllBytes(".\cbr-rar5-solid-first-image.rar", $bytes)
Get-FileHash -Algorithm SHA256 .\cbr-rar5-solid-first-image.rar
& "C:\Program Files\WinRAR\Rar.exe" t -cfg- -c- .\cbr-rar5-solid-first-image.rar
```

## Ranked fallback RAR5 archive

`cbr-rar5-ranked-fallback.rar.base64` encodes a 585-byte non-solid RAR5
archive. It verifies that selection is based on logical ranking rather than
physical archive order and that a corrupt preferred member does not prevent a
lower-ranked valid cover from being used.

Decoded archive SHA-256:

```text
fa14d0e42685f375f4200c7c0046e97901fd866c718cdaf95f2fae6723761e4e
```

Archive members, in physical order:

1. `003.png` — valid 43x63 PNG.
2. `cover.jpg` — deliberately invalid image bytes. Its explicit cover name
   gives it the highest filename-derived rank, so this exercises decode
   fallback.
3. `000.png` — valid 40x60 PNG and the naturally first usable page. This is
   the expected result.

The PNGs are original deterministic solid-color test graphics. The archive
was created in store mode with paths, attributes, timestamps, quick-open data,
and local WinRAR configuration disabled.

## Solid ComicInfo RAR5 archive

`cbr-rar5-solid-comicinfo.rar.base64` encodes a 639-byte solid RAR5 archive.
It verifies that `ComicInfo.xml` image indices address only the naturally
sorted image list, override both physical member order and explicit filename
hints, and can be discovered after decoding a prefix of a solid stream.

Decoded archive SHA-256:

```text
f17fe4207b8c7f2ee3c13ede570c9e7b7ffe2c3c169f22e65b372ca13989ee70
```

Archive members, in physical order:

1. `003.png` — valid 43x63 PNG and the expected result.
2. `00-note.txt` — a non-image member proving that metadata indices do not
   count arbitrary archive entries.
3. `ComicInfo.xml` — marks natural image index 1 as `Story FrontCover`.
4. `000.png` — valid 40x60 PNG and natural image index 0.
5. `cover.png` — valid 41x61 PNG whose explicit name would otherwise win.

The original deterministic solid-color PNGs and metadata were compressed as a
single solid stream. WinRAR's solid-file name sorting was disabled so the
listed physical order is intentional; paths, attributes, timestamps,
quick-open data, and local configuration were also disabled.

```powershell
& "C:\Program Files\WinRAR\Rar.exe" a -ma5 -s -ds -m3 -md1m -mt1 -qo- `
    -htc -ep -ai -tsm- -tsc- -tsa- -tsp- -cfg- -idq -y `
    cbr-rar5-solid-comicinfo.rar 003.png 00-note.txt ComicInfo.xml 000.png cover.png
```

## Oversized preferred-image RAR5 archive

`cbr-rar5-oversized-preferred.rar.base64` encodes a 3,042-byte non-solid RAR5
archive. Its explicit `cover.png` member expands to 64 MiB plus one byte, so it
is statically ineligible under the parser's encoded-image limit. The parser
must skip it and return the later 40x60 `000.png` fallback instead of allowing
the unusable higher-ranked member to suppress a thumbnail.

Decoded archive SHA-256:

```text
6b6fac9f08eac919a417ae15f6f4ce82b30d3e73493f1c8c948e74c3086ee3c7
```

Archive members, in physical order:

1. `cover.png` — 67,108,865 zero bytes, deliberately one byte above the
   encoded-image limit; compressed with a 128 MiB dictionary.
2. `000.png` — valid deterministic 40x60 PNG; compressed with a 128 KiB
   dictionary and expected as the result.

The two members were appended separately so their dictionary requirements are
independent. Paths, attributes, timestamps, quick-open data, and local WinRAR
configuration were disabled.
