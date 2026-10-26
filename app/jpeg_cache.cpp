#include "jpeg_cache.h"
#include "media_yield.h"
// Pinned TJpg_Decoder 1.1.0 C core, vendored without the Arduino wrapper
// to avoid its permanent 3.5 KB global workspace. See src/TJpgDec/README.md.
#include "src/TJpgDec/tjpgd.h"
#include <memory>
#include <new>
static_assert(JD_FORMAT == 1, "ChonkPod requires TJpg_Decoder RGB565 output");

bool jpeg_cacheValid(const String& path) {
  File f = SD.open(path); uint8_t h[ART_HEADER];
  return f && f.size() == ART_HEADER + uint32_t(ART_EDGE) * ART_EDGE * 2 &&
    f.read(h, sizeof(h)) == sizeof(h) && memcmp(h, "AR01", 4) == 0 &&
    (h[4] | h[5] << 8) == ART_EDGE && (h[6] | h[7] << 8) == ART_EDGE;
}
namespace {
struct Convert {
  File& input; File& output;
  uint32_t pause = 0;
  uint16_t sw = 0, sh = 0, dw = 0, dh = 0, left = 0, top = 0;
};
size_t inputJpeg(JDEC* decoder, uint8_t* buffer, size_t bytes) {
  auto& ctx = *static_cast<Convert*>(decoder->device);
  const uint32_t pos = ctx.input.position(), size = ctx.input.size();
  bytes = min<size_t>(bytes, size - min(pos, size));
  media_yield(ctx.pause);
  if (buffer) return ctx.input.read(buffer, bytes);
  return ctx.input.seek(pos + bytes) ? bytes : 0;
}
int outputJpeg(JDEC* decoder, void* bitmap, JRECT* block) {
  auto& c = *static_cast<Convert*>(decoder->device);
  if (block->right >= c.sw || block->bottom >= c.sh || block->left > block->right || block->top > block->bottom) return 0;
  // Each target coordinate samples floor(target * source / destination).
  // Intersect that mapping with this MCU: no intermediate image/framebuffer.
  int x0 = (uint32_t(block->left) * c.dw + c.sw - 1) / c.sw;
  int x1 = (uint32_t(block->right + 1) * c.dw + c.sw - 1) / c.sw;
  int y0 = (uint32_t(block->top) * c.dh + c.sh - 1) / c.sh;
  int y1 = (uint32_t(block->bottom + 1) * c.dh + c.sh - 1) / c.sh;
  const uint16_t* pixels = static_cast<uint16_t*>(bitmap);
  const int stride = block->right - block->left + 1;
  uint16_t row[ART_EDGE];
  for (int y = y0; y < y1; ++y) {
    int sy = uint32_t(y) * c.sh / c.dh - block->top;
    for (int x = x0; x < x1; ++x) {
      int sx = uint32_t(x) * c.sw / c.dw - block->left;
      row[x - x0] = pixels[sy * stride + sx];
    }
    uint32_t offset = ART_HEADER + ((c.top + y) * ART_EDGE + c.left + x0) * 2;
    const size_t n = (x1 - x0) * 2;
    if (n && (!c.output.seek(offset) || c.output.write(reinterpret_cast<uint8_t*>(row), n) != n)) return 0;
  }
  media_yield(c.pause); return 1;
}
}
bool jpeg_cacheBuild(File& source, const String& destination) {
  if (!source || source.size() > 4 * 1024 * 1024 || !source.seek(0)) return false;
  std::unique_ptr<uint8_t[]> workspace(new (std::nothrow) uint8_t[TJPGD_WORKSPACE_SIZE]);
  if (!workspace) { Serial.println("art: insufficient JPEG workspace -- fallback"); return false; }
  const String temp = destination + ".tmp"; SD.remove(temp);
  File output = SD.open(temp, FILE_WRITE);
  if (!output) return false;
  Convert ctx{source, output}; JDEC decoder = {};
  JRESULT result = jd_prepare(&decoder, inputJpeg, workspace.get(), TJPGD_WORKSPACE_SIZE, &ctx);
  bool ok = false;
  if (result == JDR_OK && decoder.width && decoder.height && decoder.width <= 4096 && decoder.height <= 4096 &&
      uint32_t(decoder.width) * decoder.height <= 8 * 1024 * 1024) {
    Serial.printf("art: JPEG %ux%u, temporary workspace %u, heap %u largest %u\n",
                  decoder.width, decoder.height, unsigned(TJPGD_WORKSPACE_SIZE), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    uint8_t scale = 0;
    while (scale < 3 && (decoder.width >> (scale + 1)) >= ART_EDGE && (decoder.height >> (scale + 1)) >= ART_EDGE) ++scale;
    ctx.sw = decoder.width >> scale; ctx.sh = decoder.height >> scale;
    if (ctx.sw >= ctx.sh) { ctx.dw = ART_EDGE; ctx.dh = max<uint16_t>(1, uint32_t(ctx.sh) * ART_EDGE / ctx.sw); }
    else { ctx.dh = ART_EDGE; ctx.dw = max<uint16_t>(1, uint32_t(ctx.sw) * ART_EDGE / ctx.sh); }
    ctx.left = (ART_EDGE - ctx.dw) / 2; ctx.top = (ART_EDGE - ctx.dh) / 2;
    const uint8_t header[] = {'A','R','0','1', uint8_t(ART_EDGE),0,uint8_t(ART_EDGE),0};
    ok = output.write(header, sizeof(header)) == sizeof(header);
    uint8_t black[320] = {};
    for (int i = 0; ok && i < ART_EDGE; ++i) { ok = output.write(black, sizeof(black)) == sizeof(black); media_yield(ctx.pause); }
    if (ok) { result = jd_decomp(&decoder, outputJpeg, scale); ok = result == JDR_OK; }
  }
  output.close();
  if (!ok || !jpeg_cacheValid(temp)) {
    Serial.printf("art: JPEG rejected/failed (decoder %d) -- fallback\n", int(result));
    SD.remove(temp); return false;
  }
  SD.remove(destination);
  if (!SD.rename(temp, destination)) { SD.remove(temp); return false; }
  return true;
}
