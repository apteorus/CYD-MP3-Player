#pragma once
// v0.9.4's log macros exist only under Arduino/IDF. Supply host no-op logging;
// decoder code and assertions remain enabled and unchanged.
#define LOGD_HELIX(...)
#define LOGI_HELIX(...)
#define LOGW_HELIX(...)
#define LOGE_HELIX(...)
