#include "titles.h"
#include "display.h"
#include "storage.h"
#include "i18n.h"
#include "theme.h"
#include "style.h"
#ifndef CYD_UPLOADER
#include "playlist.h"
#endif
#include <SD.h>
#include <esp_rom_crc.h>
#include "vlw_stream.h"

// Strip file: u16 width, u8 height, u8 bits-per-pixel (2), then height rows of
// width pixels, 2 bits each, packed MSB-first, row-major.
static const char*  FONT_NAME  = ".sys/cjk16";        // TFT_eSPI adds "/" and ".vlw"
static const char*  FONT_PATH  = "/.sys/cjk16.vlw";
static const char*  STRIP_DIR  = "/.sys/t";
static const int    MAX_W      = 720;                 // ~45 CJK characters

static uint8_t* strip = nullptr;
static int      stripW = 0;
static uint32_t stripKey = 0;

static uint32_t keyFor(const String& text) {
  return esp_rom_crc32_le(0, (const uint8_t*)text.c_str(), text.length());
}

static String pathFor(uint32_t key) {
  char p[32];
  snprintf(p, sizeof(p), "%s/%08x.ttl", STRIP_DIR, key);
  return p;
}

static inline int stripBytes(int w) { return (w * TITLE_STRIP_H * 2 + 7) / 8; }

// ---------------------------------------------------------------------------
// Boot: render missing strips
// ---------------------------------------------------------------------------
static VlwStream* renderFont = nullptr;
static TitleProgressFn renderProgress = nullptr;
static int pendingCount = 0, renderedCount = 0, failedCount = 0;
static void visitText(const char* text) {
  const String label(text);
  if (SD.exists(pathFor(keyFor(label)))) return;
  if (!renderFont) { ++pendingCount; return; }
  if (!renderFont->render(label, pathFor(keyFor(label)))) ++failedCount;
  ++renderedCount;
  if (renderProgress) renderProgress(renderedCount, pendingCount);
  if ((renderedCount % 16) == 0)
    Serial.printf("titles: %d/%d, heap %u largest %u\n", renderedCount, pendingCount,
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  yield();
}
static void collectMissing(int, const String&, const String& label) { visitText(label.c_str()); }

// A marker holding the library checksum the strips were last made for. When it
// matches, every title already has its strip and the per-title check -- one
// SD lookup per track -- is skipped.
static const char* DONE_PATH = "/.sys/t/done";

// Fixed UI text is part of the set too: a firmware update that adds or changes
// a Chinese string must not be skipped by an old marker.
static uint32_t uiCrc = 0;
static void crcOne(const char* text) {
  uiCrc = esp_rom_crc32_le(uiCrc, (const uint8_t*)text, strlen(text));
}

static String doneStamp() {
  uiCrc = 0;
  i18n_forEachChinese(crcOne);
  for (int i = 0; i < theme_count(); i++) crcOne(theme_at(i).nameZh);
  for (int i = 0; i < style_count(); i++) crcOne(style_at(i).nameZh);
  uint32_t listCrc = 0;
#ifndef CYD_UPLOADER
  listCrc = playlist_namesCrc();
#endif
  char b[28];
  snprintf(b, sizeof(b), "%08x%08x%08x", storage_libraryCrc(), uiCrc, listCrc);
  return b;
}

static bool upToDate() {
  File f = SD.open(DONE_PATH);
  if (!f) return false;
  const String s = f.readString();
  f.close();
  return s == doneStamp();
}

static void markDone() {
  File f = SD.open(DONE_PATH, FILE_WRITE);
  if (f) { f.print(doneStamp()); f.close(); }
}

static void collectUiText(const char* text) { visitText(text); }
static void collectName(const char* text) {
  for (const char* p = text; *p; ++p) if (uint8_t(*p) >= 128) { visitText(text); return; }
}
static void visitAll() {
  storage_forEach(collectMissing);
  i18n_forEachChinese(collectUiText);
  for (int i = 0; i < theme_count(); ++i) visitText(theme_at(i).nameZh);
  for (int i = 0; i < style_count(); ++i) visitText(style_at(i).nameZh);
#ifndef CYD_UPLOADER
  playlist_forEachName(collectName);
#endif
}

bool titles_prepareAll(TitleProgressFn progress) {
  if (!SD.exists(STRIP_DIR)) SD.mkdir(STRIP_DIR);
  if (upToDate()) return true;
  if (!SD.exists(FONT_PATH)) { Serial.println("titles: no font -- Latin fallback (cached strips retained)"); return false; }
  // A power loss/watchdog in this pass must never turn into a repeating boot loop.
  // Remove this file explicitly to retry after diagnosing the failure.
  const char* busy = "/.sys/font.busy";
  if (SD.exists(busy)) {
    Serial.println("titles: previous font pass interrupted; Latin fallback. Remove /.sys/font.busy to retry");
    return false;
  }
  File marker = SD.open(busy, FILE_WRITE);
  if (!marker || marker.write(uint8_t(1)) != 1) {
    Serial.println("titles: cannot arm recovery marker -- Latin fallback"); return false;
  }
  marker.close();
  Serial.printf("titles: streaming font preparation; heap %u largest %u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  bool ok = false;
  {
    VlwStream font;
    if (font.begin(FONT_PATH)) {
      pendingCount = renderedCount = failedCount = 0;
      renderFont = nullptr; visitAll();
      renderFont = &font; renderProgress = progress; visitAll();
      renderFont = nullptr; renderProgress = nullptr;
      ok = failedCount == 0;
      if (ok) markDone();
      Serial.printf("titles: %d strips written, %d failures\n", renderedCount, failedCount);
    } else Serial.println("titles: invalid/unreadable font or directory write failed -- Latin fallback");
  }
  SD.remove(busy);
  return ok;
}

// ---------------------------------------------------------------------------
// Playback: load and draw
// ---------------------------------------------------------------------------
bool titles_load(const String& text) {
  const uint32_t key = keyFor(text);
  if (strip && key == stripKey) return true;
  free(strip);
  strip = nullptr;
  stripW = 0;

  File f = SD.open(pathFor(key));
  if (!f) return false;
  uint8_t hdr[4];
  if (f.read(hdr, 4) == 4 && hdr[2] == TITLE_STRIP_H && hdr[3] == 2) {
    const int w = hdr[0] | (hdr[1] << 8);
    const int n = stripBytes(w);
    if (w > 0 && w <= MAX_W && (strip = (uint8_t*)malloc(n)) != nullptr) {
      if (f.read(strip, n) == (size_t)n) {
        stripW = w;
        stripKey = key;
      } else {
        free(strip);
        strip = nullptr;
      }
    }
  }
  f.close();
  return strip != nullptr;
}

bool titles_loaded() { return strip != nullptr; }
int  titles_width()  { return stripW; }

void titles_draw(const Rect& box, int scrollX, int gap, uint16_t fg, uint16_t bg) {
  if (!strip) return;
  // Four coverage levels blended between the two theme colours, byte-swapped
  // for pushImage (it sends the buffer as-is).
  uint16_t lut[4];
  for (int i = 0; i < 4; i++) {
    const uint16_t c = tft.alphaBlend(i * 85, fg, bg);
    lut[i] = (c >> 8) | (c << 8);
  }
  const int period = stripW + gap;
  static uint16_t line[320];
  const int w = min(box.w, 320);
  tft.startWrite();
  for (int y = 0; y < min(box.h, TITLE_STRIP_H); y++) {
    for (int x = 0; x < w; x++) {
      int vx = scrollX + x;
      if (vx >= period) vx %= period;
      uint8_t level = 0;
      if (vx < stripW) {
        const int bit = (y * stripW + vx) * 2;
        level = (strip[bit >> 3] >> (6 - (bit & 7))) & 3;
      }
      line[x] = lut[level];
    }
    tft.pushImage(box.x, box.y + y, w, 1, line);
  }
  // Anything under the strip's 18 rows belongs to the box too.
  if (box.h > TITLE_STRIP_H) {
    const uint16_t b = (bg >> 8) | (bg << 8);
    for (int x = 0; x < w; x++) line[x] = b;
    for (int y = TITLE_STRIP_H; y < box.h; y++) tft.pushImage(box.x, box.y + y, w, 1, line);
  }
  tft.endWrite();
}
