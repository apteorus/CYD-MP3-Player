#pragma once
#include "geometry.h"
#include "storage.h"
bool art_requestRefresh(); // schedules boot-only refresh; caller restarts after success
void art_prepareAll(StorageProgressFn progress);
bool art_start(int track, const Rect& view);
void art_stop();
void art_tick(uint32_t now);
