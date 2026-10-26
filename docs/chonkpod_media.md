# ChonkPod media changes

## 1. Font preparation

The reported font is structurally valid (7,284 glyphs, 1,876,519 bytes). The
previous TFT_eSPI loader allocates 87,408 bytes in unchecked glyph tables, including
one 29,136-byte block, in addition to the sprite and pending-title Strings.
Without the actual reset backtrace this is a diagnosed unsafe path, not proof
that allocation failure caused the reported reset.

Font preparation now validates the VLW header, sorted BMP codepoints, metrics,
bitmap bounds and all reads/writes before trusting them. A small glyph directory
is generated on SD in `/.sys/font.idx`; glyph alpha rows are read from the VLW
on demand. It never calls TFT_eSPI loadFont or creates a framebuffer/sprite.
Only one title is handled at a time. Temporary heap is at most 3,240 bytes for a
720x18 2-bit title strip plus small filesystem/String overhead, with a 64-byte
glyph-row buffer. The output remains the existing `/.sys/t/*.ttl` format, and
existing strips are preserved. Missing glyphs use `?`; absent/invalid fonts and
I/O/allocation failures return to built-in Latin text without stopping startup.

`/.sys/font.busy` is written and closed before font preparation. If reset or
power loss interrupts that work, the next boot skips preparation and uses the
existing cache/Latin fallback. After diagnosing the log, remove **only that
marker** to retry. Leave the rest of `/.sys` intact. Regular handled failures
remove the marker; reset during preparation leaves it deliberately in place.
Progress/heap/largest-block diagnostics are printed every 16 strips. Each glyph
and each directory chunk yields to avoid watchdog starvation. Cached titles
continue to work even if the font is removed.

Validation: host tests compile the production streaming renderer with filesystem
adapters, render a generated Latin/CJK fixture and the user's full font, and
reject malformed/truncated/missing fonts. Linux E32R28T builds validate firmware
integration. Physical SD failure, forced-reset recovery and visual alignment
still need bench tests. The font file itself is not committed or redistributed.

## 2. Metadata and index migration

`CYDLIB3` replaces `CYDLIB2`; on first boot the index is rebuilt once from MP3s.
Tab-separated fields are path, title, artist, album, track text (e.g. 3/12), raw
JPEG offset, raw JPEG length, unsync flag, and picture type. The upgrade keeps a
recoverable `library.old` during replacement. Music and old title strips are not
removed. Paths, sizes and FAT modification timestamps feed the library checksum.
If a tag is edited without changing either size or timestamp, remove only
`/.sys/library.idx` to request a rescan. Title labels remain title plus artist,
falling back to the filename. Metadata accessors read one entry on demand.

RAM remains a 4-byte offset per track plus one cached metadata entry. The existing
boot-only path scan remains transient; it does not retain album/picture Strings
for the whole library. Text reads are limited to 200 bytes per field. The parser
uses 512-byte streaming readers, validates sizes against the file, and limits
work to a 16 MiB tag / 4,096 frames / 4 MiB picture span. JPEG bodies are extracted
in 512-byte chunks. Front cover (type 3) is preferred, otherwise the first JPEG.

Supported: ID3v2.2 TT2/TP1/TAL/TRK/PIC, v2.3/v2.4 TIT2/TPE1/TALB/TRCK/APIC;
Latin-1, UTF-8, UTF-16 BOM/BE, extended headers, grouping/data-length prefixes,
v2.2/3 tag unsync and v2.4 frame/tag unsync. Compressed/encrypted frames, compressed
v2.2 tags, non-JPEG artwork and external URL pictures are skipped. ID3v1 encoding
guessing remains deliberately unsupported. Large pictures are not copied into RAM.

Production-parser host tests cover all three tag versions, Unicode/Latin-1,
artwork after all text fields, preferred front cover, >7 KB unsynchronised pictures,
byte-for-byte extraction, both extended-header conventions, unsupported compression
and truncated tags. Both firmware images compile with the upgraded index.
