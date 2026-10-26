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
