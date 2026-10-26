#pragma once
#include <Arduino.h>
#include <SD.h>

// Validated, SD-backed VLW glyph directory. No TFT_eSPI loadFont() or sprite.
// Only used before radio startup; releases files on scope exit.
class VlwStream {
 public:
  bool begin(const char* path);
  bool render(const String& text, const String& destination);
 private:
  File font, directory;
  uint32_t glyphs = 0, fontSize = 0, pause = 0;
  int ascent = 0;
  bool warned = false;
  bool reject(const char* reason);
  struct Glyph { uint16_t code; uint8_t h, w, advance; int8_t dx; int16_t dy; uint32_t offset; };
  bool lookup(uint32_t cp, Glyph& g);
  bool measure(const String& text, int& width);
};
