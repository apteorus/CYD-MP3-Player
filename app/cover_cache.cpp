#include "cover_cache.h"
#include "jpeg_cache.h"
#include "media_yield.h"

namespace {
uint64_t hashBytes(uint64_t h, const void* data, size_t n) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  while (n--) { h ^= *p++; h *= 1099511628211ULL; } return h;
}
uint64_t hashText(const String& s) { return hashBytes(14695981039346656037ULL, s.c_str(), s.length()); }
String hex(uint64_t n) { char b[17]; snprintf(b, sizeof(b), "%016llx", (unsigned long long)n); return b; }
String reference(const String& track) { return "/.sys/art/" + hex(hashText(track)) + ".ref"; }
bool assetName(const String& name) {
  if (name.length() != 20 || !name.endsWith(".rgb")) return false;
  for (int i = 0; i < 16; ++i) if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return false;
  return true;
}
bool stat(const String& path, uint32_t& size, uint32_t& time) {
  if (path.isEmpty()) { size = time = 0; return false; }
  File f = SD.open(path); if (!f) { size = time = 0; return false; }
  size = f.size(); time = f.getLastWrite(); return true;
}
String folderCover(const String& track) {
  String dir = track.substring(0, track.lastIndexOf('/'));
  while (dir == "/music" || dir.startsWith("/music/")) {
    String cover = dir + "/cover.jpg";
    if (SD.exists(cover)) return cover;
    dir = dir.substring(0, dir.lastIndexOf('/'));
  }
  return String();
}
String signature(const String& track, const String& folder, const Id3Info& info) {
  uint32_t fields[9] = {};
  stat(track, fields[0], fields[1]); stat(folder, fields[2], fields[3]);
  fields[4] = info.picture.offset; fields[5] = info.picture.length;
  fields[6] = info.picture.unsync; fields[7] = 1; // RGB cache/conversion version
  fields[8] = info.picture.type;
  uint64_t h = hashBytes(hashText(track), fields, sizeof(fields));
  h = hashBytes(h, folder.c_str(), folder.length()); return "AR1 " + hex(h);
}
bool readRef(const String& path, String& stamp, String& asset) {
  File f = SD.open(path);
  if (!f || f.size() > 64) return false;
  stamp = f.readStringUntil('\n'); asset = f.readStringUntil('\n');
  return stamp.startsWith("AR1 ") && stamp.length() == 20 && (asset.isEmpty() || assetName(asset));
}
bool writeRef(const String& path, const String& stamp, const String& asset) {
  String temp = path + ".tmp"; SD.remove(temp);
  File f = SD.open(temp, FILE_WRITE);
  if (!f) return false;
  bool ok = f.print(stamp + "\n" + asset + "\n") == stamp.length() + asset.length() + 2;
  f.close(); if (!ok) { SD.remove(temp); return false; }
  SD.remove(path); return SD.rename(temp, path);
}
String convert(File& source) {
  if (!source || source.size() > 4 * 1024 * 1024 || !source.seek(0)) return String();
  uint64_t hash = 14695981039346656037ULL;
  uint8_t data[512]; uint32_t pause = millis();
  while (source.position() < source.size()) {
    size_t got = source.read(data, sizeof(data)); if (!got) return String();
    hash = hashBytes(hash, data, got); media_yield(pause);
  }
  String name = hex(hash) + ".rgb", path = "/.sys/art/" + name;
  if (jpeg_cacheValid(path)) return name;
  // Avoid retrying unsupported pictures once for every song of the same album.
  if (SD.exists(path + ".bad")) return String();
  if (jpeg_cacheBuild(source, path)) return name;
  File bad = SD.open(path + ".bad", FILE_WRITE); if (bad) bad.write(uint8_t(1));
  return String();
}
}
String cover_asset(const String& trackPath, bool* stale) {
  if (stale) *stale = false;
  String stamp, asset;
  if (!readRef(reference(trackPath), stamp, asset)) {
    if (stale) *stale = true;
    return String();
  }
  if (asset.isEmpty()) return String();
  String path = "/.sys/art/" + asset;
  if (jpeg_cacheValid(path)) return path;
  if (stale) *stale = true;
  return String();
}
bool cover_prepare(const String& trackPath, const Id3Info& info) {
  if (!SD.exists("/.sys/art") && !SD.mkdir("/.sys/art")) return false;
  const String ref = reference(trackPath), busy = ref + ".busy";
  if (SD.exists(busy)) {
    Serial.printf("art: interrupted conversion for %s; placeholder (remove %s to retry)\n", trackPath.c_str(), busy.c_str());
    SD.remove(ref); return false;
  }
  const String folder = folderCover(trackPath), stamp = signature(trackPath, folder, info);
  String oldStamp, asset;
  if (readRef(ref, oldStamp, asset) && oldStamp == stamp &&
      (asset.isEmpty() || jpeg_cacheValid("/.sys/art/" + asset))) return true;
  File marker = SD.open(busy, FILE_WRITE);
  if (!marker || marker.write(uint8_t(1)) != 1) return false;
  marker.close(); asset = String();
  if (info.picture.length) {
    // Only a small streamed JPEG temp, never the original MP3 or a RAM blob.
    const char* temp = "/.sys/art/source.tmp.jpg"; SD.remove(temp);
    File input = SD.open(trackPath), output = SD.open(temp, FILE_WRITE);
    bool extracted = id3_extractJpeg(input, info.picture, output);
    input.close(); output.close();
    if (extracted) { File jpeg = SD.open(temp); asset = convert(jpeg); }
    SD.remove(temp);
  }
  if (asset.isEmpty() && !folder.isEmpty()) { File jpeg = SD.open(folder); asset = convert(jpeg); }
  bool ok = writeRef(ref, stamp, asset);
  if (ok) SD.remove(busy); // failed reference writes remain safe/disabled on next boot
  if (!asset.isEmpty()) Serial.printf("art: cached %s -> %s\n", trackPath.c_str(), asset.c_str());
  return ok;
}
