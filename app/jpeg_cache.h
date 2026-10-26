#pragma once
#include <Arduino.h>
#include <SD.h>

static const uint16_t ART_EDGE = 160;
static const uint32_t ART_HEADER = 8;
// AR01, width/height u16 LE, then row-major RGB565 u16 LE. Black letterbox.
bool jpeg_cacheValid(const String& path);
bool jpeg_cacheBuild(File& source, const String& destination);
