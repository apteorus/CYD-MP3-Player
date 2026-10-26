#ifndef CYD_UPLOADER
#include "artwork.h"
#include "cover_cache.h"
#include "media_yield.h"

namespace {
constexpr const char* CHECKPOINT = "/.sys/art/prepared";
constexpr const char* REFRESH = "/.sys/art/refresh";
// Explicit little-endian format: magic/version, library+cover fingerprint,
// track count, checksum. Bump magic when preparation or RGB/ref formats change.
void put(uint8_t* p, uint32_t n) { for (int i=0;i<4;++i) p[i] = n >> (8*i); }
uint32_t get(const uint8_t* p) { uint32_t n=0; for(int i=0;i<4;++i) n |= uint32_t(p[i]) << (8*i); return n; }
void record(uint8_t* b) {
  memcpy(b, "ACP1", 4); put(b+4, storage_artworkCrc()); put(b+8, storage_trackCount());
  put(b+12, get(b) ^ get(b+4) ^ get(b+8) ^ 0xa17c39e5U);
}
bool current() {
  uint8_t expected[16], actual[16]; record(expected);
  File f=SD.open(CHECKPOINT);
  return f && f.size()==sizeof(actual) && f.read(actual,sizeof(actual))==sizeof(actual) &&
    memcmp(expected,actual,sizeof(actual))==0;
}
bool save() {
  const String temp=String(CHECKPOINT)+".tmp"; SD.remove(temp);
  uint8_t b[16]; record(b); File f=SD.open(temp, FILE_WRITE);
  if (!f) return false;
  bool ok=f.write(b,sizeof(b))==sizeof(b); f.close();
  if (!ok) { SD.remove(temp); return false; }
  // A power loss at replacement leaves no valid checkpoint: repeat safe scan.
  SD.remove(CHECKPOINT); return SD.rename(temp,CHECKPOINT);
}
bool clearReferences() {
  File dir=SD.open("/.sys/art"); if (!dir) return false;
  uint32_t pause=millis(); bool ok=true;
  for (File f=dir.openNextFile(); f; f=dir.openNextFile()) {
    String name=f.name(); bool directory=f.isDirectory(); f.close();
    if (name.lastIndexOf('/')>=0) name=name.substring(name.lastIndexOf('/')+1);
    if (!directory && (name.endsWith(".ref") || name.endsWith(".ref.busy") || name.endsWith(".rgb.bad")))
      if (!SD.remove("/.sys/art/"+name)) ok=false;
    media_yield(pause);
  }
  return ok;
}
StorageProgressFn prepareProgress=nullptr;
int prepareCount=0, prepareFailed=0;
void prepare(int i,const String& path,const Id3Info& info) {
  if (!cover_prepare(path,info)) ++prepareFailed;
  ++prepareCount;
  if (prepareProgress) prepareProgress(i+1,storage_trackCount());
  if ((prepareCount & 15)==0) Serial.printf("art: %d tracks checked, heap %u largest %u\n",prepareCount,ESP.getFreeHeap(),ESP.getMaxAllocHeap());
  yield();
}
}
bool art_requestRefresh() {
  if (!SD.exists("/.sys/art") && !SD.mkdir("/.sys/art")) return false;
  SD.remove(REFRESH); File f=SD.open(REFRESH,FILE_WRITE);
  if (!f) return false;
  const bool ok=f.write(uint8_t(1))==1; f.close(); return ok;
}
void art_prepareAll(StorageProgressFn progress) {
  if (!SD.exists("/.sys/art") && !SD.mkdir("/.sys/art")) {
    Serial.println("art: cache unavailable -- placeholders"); return;
  }
  if (SD.exists(REFRESH)) {
    if (SD.exists(CHECKPOINT) && !SD.remove(CHECKPOINT)) {
      Serial.println("art: cannot invalidate checkpoint -- refresh deferred"); return;
    }
    if (!clearReferences() || !SD.remove(REFRESH)) {
      Serial.println("art: refresh cleanup failed -- retry next boot"); return;
    }
    // Consume the request BEFORE decoding. A new decoder crash must retain
    // its per-track busy marker, rather than being retried on every boot.
    Serial.println("art: manual refresh; old RGB assets reused when valid");
  }
  if (current()) {
    Serial.println("art: checkpoint current -- skipped full artwork traversal"); return;
  }
  SD.remove(CHECKPOINT);
  prepareCount=prepareFailed=0; prepareProgress=progress;
  if (!storage_forEachInfo(prepare)) ++prepareFailed;
  prepareProgress=nullptr;
  if (!prepareFailed && !save()) Serial.println("art: checkpoint write failed -- safe rescan next boot");
  Serial.printf("art: prepared %d tracks, %d I/O/interrupted failures; JPEG workspace released\n",prepareCount,prepareFailed);
}
#endif
