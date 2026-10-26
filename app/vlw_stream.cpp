#include "vlw_stream.h"
#include "titles.h"
#include "media_yield.h"
#include <memory>
#include <new>

static uint32_t be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
// Reject malformed UTF-8 rather than indexing off the end of a String.
static uint32_t nextCode(const String& text, unsigned& pos) {
  const uint8_t a = text[pos++];
  if (a < 128) return a;
  int extra = a >= 0xC2 && a <= 0xDF ? 1 : a >= 0xE0 && a <= 0xEF ? 2 : a >= 0xF0 && a <= 0xF4 ? 3 : 0;
  if (!extra || pos + extra > text.length()) return '?';
  uint32_t cp = a & ((1 << (6 - extra)) - 1);
  for (int i = 0; i < extra; ++i) {
    uint8_t b = text[pos];
    if ((b & 0xC0) != 0x80) return '?';
    ++pos; cp = cp << 6 | (b & 63);
  }
  return extra == 3 || cp > 0xFFFF || (cp >= 0xD800 && cp <= 0xDFFF) ||
         (extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ? '?' : cp;
}
bool VlwStream::reject(const char* reason) {
  if (!warned) Serial.printf("titles: %s; heap %u largest %u -- Latin fallback\n", reason, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  warned = true; return false;
}
bool VlwStream::begin(const char* path) {
  directory.close(); font.close(); glyphs = 0; warned = false; pause = millis();
  font = SD.open(path);
  uint8_t header[24];
  if (!font || font.read(header, sizeof(header)) != sizeof(header)) return reject("font header open/read failed");
  glyphs = be32(header); ascent = be32(header + 16);
  const uint32_t version = be32(header + 4), size = font.size();
  fontSize = size;
  if (!glyphs || glyphs > 16000 || version != 11 || be32(header + 8) < 1 || be32(header + 8) > 64 ||
      ascent < 1 || ascent > 64 || be32(header + 20) > 64 ||
      uint64_t(24) + uint64_t(glyphs) * 28 > size) return reject("invalid VLW header/version/glyph count");
  SD.remove("/.sys/font.tmp");
  File out = SD.open("/.sys/font.tmp", FILE_WRITE);
  if (!out) return reject("cannot create font directory");
  static_assert(sizeof(Glyph) == 12, "VLW SD directory record size changed");
  // Direct BMP lookup uses 768 KiB on SD, zero RAM tables, and one seek per
  // glyph rather than a dozen random SD reads per binary-search lookup.
  uint8_t zero[512] = {};
  for (uint32_t offset = 0; offset < 65536UL * sizeof(Glyph); offset += sizeof(zero)) {
    if (out.write(zero, sizeof(zero)) != sizeof(zero)) return reject("font directory preallocation failed");
    media_yield(pause);
  }
  uint32_t bitmap = 24 + glyphs * 28, last = 0;
  for (uint32_t i = 0; i < glyphs; ++i) {
    uint8_t raw[28];
    if (font.read(raw, sizeof(raw)) != sizeof(raw)) return reject("font metrics truncated/unreadable");
    uint32_t cp = be32(raw), h = be32(raw + 4), w = be32(raw + 8), adv = be32(raw + 12);
    int32_t dy = int32_t(be32(raw + 16)), dx = int32_t(be32(raw + 20));
    if (!cp || cp <= last || cp > 0xFFFF || (cp >= 0xD800 && cp <= 0xDFFF) ||
        h > 64 || w > 64 || adv > 64 || dy < -64 || dy > 64 || dx < -64 || dx > 64 ||
        uint64_t(bitmap) + w * h > size) return reject("invalid glyph metric/bitmap range");
    Glyph g = {}; g.code = cp; g.h = h; g.w = w; g.advance = adv;
    g.dx = dx; g.dy = dy; g.offset = bitmap;
    if (!out.seek(cp * sizeof(Glyph)) || out.write(reinterpret_cast<uint8_t*>(&g), sizeof(g)) != sizeof(g)) return reject("font directory seek/write failed");
    bitmap += w * h; last = cp;
    if ((i & 31) == 0) media_yield(pause);
  }
  out.close();
  SD.remove("/.sys/font.idx");
  if (!SD.rename("/.sys/font.tmp", "/.sys/font.idx")) return reject("font directory rename failed");
  directory = SD.open("/.sys/font.idx");
  Serial.printf("titles: validated %lu glyphs, %lu bytes; streaming directory, no glyph heap tables\n",
                (unsigned long)glyphs, (unsigned long)size);
  return (directory && directory.size() == 65536UL * sizeof(Glyph)) || reject("font directory open/length validation failed");
}
bool VlwStream::lookup(uint32_t cp, Glyph& g) {
  if (cp == ' ') { g = {}; g.advance = 5; return true; }
  if (cp <= 0xFFFF) {
    if (!directory.seek(cp * sizeof(Glyph)) || directory.read(reinterpret_cast<uint8_t*>(&g), sizeof(g)) != sizeof(g)) return reject("glyph directory read failed");
    if (cp && g.code == cp) {
      bool valid = g.h <= 64 && g.w <= 64 && g.advance <= 64 && g.dx >= -64 && g.dx <= 64 &&
        g.dy >= -64 && g.dy <= 64 && g.offset >= 24 + glyphs * 28 &&
        uint64_t(g.offset) + uint32_t(g.h) * g.w <= fontSize;
      return valid || reject("corrupt glyph directory entry");
    }
  }
  if (cp != '?') return lookup('?', g);
  g = {}; g.advance = 5; return true;
}
bool VlwStream::measure(const String& text, int& width) {
  width = 0;
  for (unsigned p = 0; p < text.length();) {
    Glyph g;
    if (!lookup(nextCode(text, p), g)) return false;
    if (!width && g.dx < 0) width = -g.dx;
    width += g.advance;
    if (width >= 720) break;
  }
  width = constrain(width, 1, 720); return true;
}
bool VlwStream::render(const String& text, const String& destination) {
  if (!font || !directory || text.length() > 2048) return false;
  int width;
  if (!measure(text, width)) return false;
  const size_t bytes = (width * TITLE_STRIP_H + 3) / 4;
  std::unique_ptr<uint8_t[]> strip(new (std::nothrow) uint8_t[bytes]());
  if (!strip) return reject("title strip allocation failed (maximum 3240 bytes)");
  int cursor = 0;
  for (unsigned p = 0; p < text.length() && cursor < width;) {
    Glyph g;
    if (!lookup(nextCode(text, p), g)) return false;
    if (!cursor && g.dx < 0) cursor = -g.dx;
    uint8_t row[64];
    for (unsigned y = 0; y < g.h; ++y) {
      if (!font.seek(g.offset + y * g.w) || font.read(row, g.w) != g.w) return reject("glyph bitmap read failed");
      int ty = ascent - g.dy + y;
      if (ty < 0 || ty >= TITLE_STRIP_H) continue;
      for (unsigned x = 0; x < g.w; ++x) {
        int tx = cursor + g.dx + x;
        if (tx < 0 || tx >= width) continue;
        int pixel = ty * width + tx, shift = 6 - ((pixel & 3) * 2);
        uint8_t level = row[x] >> 6;
        // Combining/overlapping glyphs retain the stronger coverage.
        uint8_t old = (strip[pixel / 4] >> shift) & 3;
        if (level > old) strip[pixel / 4] = (strip[pixel / 4] & ~(3 << shift)) | (level << shift);
      }
    }
    cursor += g.advance; media_yield(pause);
  }
  const String temp = destination + ".tmp";
  SD.remove(temp);
  File out = SD.open(temp, FILE_WRITE);
  const uint8_t header[] = {uint8_t(width), uint8_t(width >> 8), TITLE_STRIP_H, 2};
  bool ok = out && out.write(header, 4) == 4 && out.write(strip.get(), bytes) == bytes;
  out.close();
  if (!ok) { SD.remove(temp); return reject("title strip write failed"); }
  SD.remove(destination);
  return SD.rename(temp, destination) || reject("title strip rename failed");
}
