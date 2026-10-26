#pragma once
#include "id3.h"
// Boot-only extraction/conversion. Source changes invalidate the reference;
// identical JPEG contents share a single small RGB565 asset.
bool cover_prepare(const String& trackPath, const Id3Info& info);
// Playback lookup reads only the tiny track reference, never the MP3/APIC.
String cover_asset(const String& trackPath);
