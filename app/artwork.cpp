#ifndef CYD_UPLOADER
#include "artwork.h"
#include "cover_cache.h"
#include "jpeg_cache.h"
#include "audio.h"
#include "display.h"
#include "theme.h"
#include "ui.h"

static StorageProgressFn prepareProgress = nullptr;
static int prepareCount = 0, prepareFailed = 0;
static void prepare(int i, const String& path, const Id3Info& info) {
  if (!cover_prepare(path, info)) ++prepareFailed;
  ++prepareCount;
  if (prepareProgress) prepareProgress(i + 1, storage_trackCount());
  if ((prepareCount & 15) == 0) Serial.printf("art: %d tracks checked, heap %u largest %u\n", prepareCount, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  yield();
}
void art_prepareAll(StorageProgressFn progress) {
  prepareCount = prepareFailed = 0; prepareProgress = progress;
  if (!storage_forEachInfo(prepare)) ++prepareFailed;
  prepareProgress = nullptr;
  Serial.printf("art: prepared %d tracks, %d I/O/interrupted failures; JPEG workspace released\n", prepareCount, prepareFailed);
}
static File image;
static Rect target;
static int row = 0;
static uint32_t nextRow = 0;
void art_stop() { image.close(); row = 0; }
bool art_start(int track, const Rect& view) {
  art_stop();
  if (view.w <= 0 || view.h <= 0) return false;
  const String asset = cover_asset(storage_trackPath(track));
  if (asset.isEmpty()) return false;
  image = SD.open(asset);
  if (!image) return false;
  int side = min(min(view.w, view.h), 320);
  target = {view.x + (view.w - side) / 2, view.y + (view.h - side) / 2, side, side};
  tft.fillRect(view.x, view.y, view.w, view.h, theme().bg);
  nextRow = 0; return true;
}
void art_tick(uint32_t now) {
  if (!image || audio_bufferLow() || int32_t(now - nextRow) < 0) return;
  // One SD row and one display row per slice. No JPEG decoder lives at runtime.
  uint16_t source[ART_EDGE], output[320];
  unsigned sy = uint32_t(row) * ART_EDGE / target.h;
  if (!image.seek(ART_HEADER + sy * ART_EDGE * 2) ||
      image.read(reinterpret_cast<uint8_t*>(source), sizeof(source)) != sizeof(source)) {
    Serial.println("art: cache read failed -- placeholder");
    tft.fillRect(target.x, target.y, target.w, target.h, theme().bg);
    ui_icon(ICON_NOTE, target.cx(), target.cy(), 48, theme().dim, theme().bg);
    art_stop(); return;
  }
  for (int x = 0; x < target.w; ++x) {
    uint16_t pixel = source[uint32_t(x) * ART_EDGE / target.w];
    output[x] = (pixel << 8) | (pixel >> 8); // TFT pushImage expects swapped words
  }
  const bool swap = tft.getSwapBytes(); tft.setSwapBytes(false);
  tft.pushImage(target.x, target.y + row, target.w, 1, output);
  tft.setSwapBytes(swap);
  nextRow = now + 4;
  if (++row >= target.h) art_stop();
}
#endif
