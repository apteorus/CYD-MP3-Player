# ChonkPod media changes

## 1. Font preparation

The reported font is structurally valid (7,284 glyphs, 1,876,519 bytes). The
previous TFT_eSPI loader allocates 87,408 bytes in unchecked glyph tables, including
one 29,136-byte block, in addition to the sprite and pending-title Strings.
Without the actual reset backtrace this is a diagnosed unsafe path, not proof
that allocation failure caused the reported reset.

Font preparation now validates the VLW header, sorted BMP codepoints, metrics,
bitmap bounds and all reads/writes before trusting them. A small glyph directory
is generated on SD in `/.sys/font.idx` (768 KiB, direct BMP slots, one lookup
read per character rather than repeated binary-search seeks); glyph alpha rows are read from the VLW
on demand. It never calls TFT_eSPI loadFont or creates a framebuffer/sprite.
Only one title is handled at a time. Temporary heap is at most 3,240 bytes for a
720x18 2-bit title strip plus small filesystem/String overhead, with a 64-byte
glyph-row buffer. The output remains the existing `/.sys/t/*.ttl` format, and
valid existing strips are preserved. Truncated/invalid strips are replaced only
after a complete new temporary strip has been written; a damaged playback strip
invalidates the completion marker for repair on the next boot. Allocation failure
does not delete a valid cached strip. Missing glyphs use `?`; absent/invalid fonts and
I/O/allocation failures return to built-in Latin text without stopping startup.

`/.sys/font.busy` is written and closed before font preparation. If reset or
power loss interrupts that work, the next boot skips preparation and uses the
existing cache/Latin fallback. After diagnosing the log, remove **only that
marker** to retry. Leave the rest of `/.sys` intact. Handled failures after a successfully armed pass
remove the marker; a failed marker write can conservatively leave an empty sentinel; reset during preparation leaves it deliberately in place.
Progress/heap/largest-block diagnostics are printed every 16 strips. Each glyph
and each directory chunk periodically blocks for a FreeRTOS tick to let idle/
watchdog tasks run. A plain yield alone is not relied on for watchdog safety. Cached titles
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

RAM remains a 4-byte offset per track plus one cached metadata entry. Index
rows are bounded to 4,096 bytes; the public library checksum covers the actual
index contents so metadata-only rescans invalidate title preparation. The existing
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

## 3. Album artwork

Choose **Setup → Visuals → Album artwork** with the **Classic** layout. Existing
installations default to Animated GIFs and retain the saved background selection.
Visuals also offers Off. List and Big buttons layouts retain their existing use
of screen space. The Classic art area includes an album-name / track-number
caption. Album names use the same safe pre-rendered strip cache as song titles.
Existing folder queues and file-path ordering are preserved; this does not create
a separate ID3 album browser or reorder playlists by tag track number.

Priority per track: preferred embedded JPEG APIC/PIC → nearest `cover.jpg` in the
track directory or its parents up to `/music` → music-note placeholder. PNG,
progressive/unsupported JPEG variants, pictures above 4 MiB, dimensions above
4096 on either axis or more than 8 Mi pixels, malformed data and I/O failures
fall back. JPEG decode errors are logged. No external URL images are fetched.

Artwork is prepared once **before** audio/Bluetooth starts, with visible progress.
The first boot can take several minutes for a large library; allow progress to
finish. Later boots check small source references instead. Newly uploaded music
is processed when returning from upload mode, which already restarts the player.
Folder covers added on a PC need a reboot. No JPEG decoder runs during playback.

The proven TJpgDec R0.03 C core from **Bodmer TJpg_Decoder 1.1.0** is bundled under
`app/src/TJpgDec`, with upstream copyright, license and checksums. No extra JPEG
library needs installing on Windows. The Arduino wrapper is deliberately omitted:
its global object would permanently reserve about 3.5 KiB. The decoder workspace
is a checked **3,500-byte temporary allocation**, freed after each conversion.
MCU blocks are sampled directly into a 160×160, aspect-preserving, letterboxed
RGB565 file: **51,208 bytes** including its `AR01` header. There is no full image
framebuffer. During playback, one 160-pixel source row and one output row (up to
320 pixels) use **960 bytes of temporary stack**, painted in 4 ms slices with
backoff while the MP3 ring is low. TFT byte-swap state is restored after each row.

`/.sys/art/` holds content-hashed `.rgb` assets and tiny per-track `.ref` files.
Identical JPEG bytes share an asset, including between embedded and folder covers.
Path, size, modification time and APIC span/flags invalidate a source reference;
changing a folder cover invalidates its fallback users. When replacing bytes
while deliberately keeping both size and modification time unchanged, remove the
affected reference file to force reprocessing. `.rgb.bad` markers avoid
repeatedly decoding the same unsupported JPEG. Temporary JPEG extraction is
streamed to SD and removed; the original MP3 is never modified. Stale assets
are retained rather than deleted during playback; SD space includes FAT cluster
rounding and references, not just the pixel payloads.

A `.ref.busy` marker protects each track's conversion. A reset/power cut or failed
reference write leaves that track disabled on the next boot, with its marker path
printed over serial. Delete that **specific marker** only after checking the
source/SD issue to retry. Handled unsupported pictures instead produce a normal
folder/placeholder reference. To retry a previously unsupported JPEG without
changing its bytes, remove its `.rgb.bad` and affected `.ref` files. A damaged or
missing RGB cache is rebuilt on the next boot. A playback read failure restores
the note placeholder immediately. Malformed reference filenames cannot escape
`/.sys/art/`.

The four-file SD budget remains unchanged. Boot font rendering uses two font
files + one index + one output strip at peak. Cover conversion uses an index,
JPEG source and output. The recursive player/uploader scans now close directory
entry handles before descending, allowing the documented two subfolder levels
without silently exhausting this limit.

## Validation and memory evidence

Run on Linux with existing gcc/g++, Python/Pillow:

```sh
MEDIA_PYTHON=python3 MEDIA_SANITIZE=1 UBSAN_OPTIONS=halt_on_error=1 bash tools/test_media.sh
# Optional last argument: a privately supplied full .vlw font (not committed).
```

The tests compile the production font/parser/index/extraction/cache code, and the
actual bundled JPEG decoder, using host filesystem/display adapters. They check
allocation failure, corrupt/truncated font/directory data, persisted font recovery,
retained title strips, complete metadata roundtrips and index migration/rollback,
four-file/deep-folder scanning, APIC unsync extraction, all JPEG scale factors,
RGB565 colors/aspect padding, progressive rejection, cache deduplication, source
invalidation, damaged caches and interrupted-conversion placeholders. Address and
undefined-behavior sanitizers pass. The user's full 7,284-glyph font was validated
and rendered in these tests; it is not redistributed in the repository.

Both player and uploader build on Linux for E32R28T and the original default
ST7789 CYD, core 3.3.10 / TFT_eSPI 2.5.43. Final linker figures:

| Board / image | Flash bytes | Static RAM bytes |
|---|---:|---:|
| E32R28T player | 1,349,529 (68%) | 56,336 (17%) |
| E32R28T uploader | 1,149,141 (58%) | 48,980 (14%) |
| Original ST7789 player | 1,349,437 (68%) | 56,336 (17%) |
| Original ST7789 uploader | 1,149,073 (58%) | 48,980 (14%) |

Each image fits its 1,966,080-byte min_spiffs slot; generated partition tables
were checked for player 0x10000 and uploader 0x1F0000. No hardware was flashed. Relative to the seek-stable build, all three media changes add
only **160 bytes of player static RAM** (56,176 → 56,336) and **80 bytes of uploader
static RAM** (48,900 → 48,980). These are linker figures, not measured runtime
heap. Additional bounded per-entry Strings, filesystem buffers and stack coexist
with the temporary allocations described above; the figures are not a bound on
total process memory. The old library-wide pending title Strings, 13 KiB sprite
and 85 KiB glyph tables are gone. MP3 decoder, ring, pairing, SD buses and touch
calibration architecture are unchanged.

## Windows update and bench checklist

Close the serial monitor, then from the existing repository:

```powershell
git pull --ff-only
.\tools\flash_windows.ps1 -WithUploader -Python python
# Once both local builds succeed:
.\tools\flash_windows.ps1 -WithUploader -Flash -Port COM4 -Python python
arduino-cli monitor --port COM4 --config baudrate=115200
```

The current banner should end in `-media5`; see
[SDK fix and exact current update steps](chonkpod_a2dp_fix.md) and
[performance changes and artwork checkpoint](chonkpod_performance.md). There is no automatic erase and no need to
change the existing TFT setup. Keep the original full-flash backup. Copy the
previously generated font back to `/.sys/cjk16.vlw` with power off; if a font busy
marker exists, inspect the log and remove only that marker before retrying. Do not
remove existing title strips. The index upgrade and first font/artwork pass run
before normal playback; retain the startup log and let progress complete.

Physical validation is still pending: confirm CJK glyphs and Latin fallback,
missing/corrupt-font boot with no repeated reset, one forced-reset recovery during
font preparation, embedded and folder artwork, portrait/landscape/masks/colors,
caption metadata, List/Big/GIF modes, cache reuse after restart, adding music and
changing covers through SD/upload mode, sustained Jawbone A2DP playback, seeking,
track changes, reconnect, SD timings, minimum heap and loop-task stack high-water.
Repeat representative checks on original boards. Local Windows builds/bench tests
of this media revision have not been performed by the cloud agent.
