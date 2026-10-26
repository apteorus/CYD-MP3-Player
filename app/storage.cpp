#include "storage.h"
#include "board.h"
#include "id3.h"
#include <SPI.h>
#include <vector>
#include <algorithm>
#include <esp_rom_crc.h>

static SPIClass sdSPI(VSPI);

static const char* INDEX_PATH = "/.sys/library.idx";
static const char* INDEX_TMP  = "/.sys/library.tmp";
static const char* INDEX_MAGIC = "CYDLIB3";
static const int   MAX_TRACKS = 2000;

static uint32_t* offsets = nullptr;
static int       trackCount = 0;
static uint32_t  libraryCrc = 0;

static SemaphoreHandle_t mutex = nullptr;
static int    cacheIndex = -1;
static String cachePath, cacheLabel;
static Id3Info cacheInfo;

// ---------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------
struct Found { String path; uint32_t size, modified; };

static void scanDir(const String& path, int depth, std::vector<Found>& out) {
  File d = SD.open(path);
  if (!d || !d.isDirectory()) return;
  for (File f = d.openNextFile(); f && (int)out.size() < MAX_TRACKS; f = d.openNextFile()) {
    const char* name = f.name();
    if (name[0] != '.') {
      const String p = path + "/" + name;
      if (f.isDirectory()) {
        if (depth < 2) scanDir(p, depth + 1, out);
      } else {
        String low = p;
        low.toLowerCase();
        if (low.endsWith(".mp3")) out.push_back({ p, (uint32_t)f.size(), (uint32_t)f.getLastWrite() });
      }
    }
    f.close();
  }
  d.close();
}

static String fileBaseName(const String& path) {
  String name = path.substring(path.lastIndexOf('/') + 1);
  const int dot = name.lastIndexOf('.');
  return dot > 0 ? name.substring(0, dot) : name;
}

static String makeLabel(const String& path, const String& title, const String& artist) {
  String label = title.length() ? title : fileBaseName(path);
  if (artist.length()) label += " - " + artist;
  return label;
}

// Field reads stay bounded to one index row; no library-wide metadata Strings.
static String field(const String& line, int wanted) {
  int start = 0;
  for (int i = 0; i < wanted; ++i) {
    start = line.indexOf('\t', start);
    if (start < 0) return String();
    ++start;
  }
  int end = line.indexOf('\t', start);
  return line.substring(start, end < 0 ? line.length() : end);
}
static void splitInfo(const String& line, String& path, Id3Info& info) {
  path = field(line, 0); info = Id3Info();
  info.title = field(line, 1); info.artist = field(line, 2);
  info.album = field(line, 3); info.track = field(line, 4);
  info.picture.offset = strtoul(field(line, 5).c_str(), nullptr, 10);
  info.picture.length = strtoul(field(line, 6).c_str(), nullptr, 10);
  info.picture.unsync = field(line, 7) == "1";
  info.picture.type = strtoul(field(line, 8).c_str(), nullptr, 10);
}
static void splitLine(const String& line, String& path, String& title, String& artist) {
  path = field(line, 0); title = field(line, 1); artist = field(line, 2);
}

// ---------------------------------------------------------------------------
// Index file
// ---------------------------------------------------------------------------
static bool indexIsCurrent(int count, uint32_t crc) {
  File f = SD.open(INDEX_PATH);
  if (!f) return false;
  const String header = f.readStringUntil('\n');
  f.close();
  char expect[40];
  snprintf(expect, sizeof(expect), "%s %d %08x", INDEX_MAGIC, count, crc);
  return header == expect;
}

static bool buildIndex(const std::vector<Found>& tracks, uint32_t crc, StorageProgressFn progress) {
  SD.remove(INDEX_TMP);
  File out = SD.open(INDEX_TMP, FILE_WRITE);
  if (!out) return false;
  out.printf("%s %d %08x\n", INDEX_MAGIC, (int)tracks.size(), crc);

  const uint32_t t0 = millis();
  int tagged = 0;
  for (size_t i = 0; i < tracks.size(); i++) {
    Id3Info tag;
    File f = SD.open(tracks[i].path);
    if (f) {
      if (id3_read(f, tag)) tagged++;
      f.close();
    }
    out.print(tracks[i].path);
    out.print('\t');
    out.print(tag.title);
    out.print('\t');
    out.print(tag.artist); out.print('\t');
    out.print(tag.album); out.print('\t');
    out.print(tag.track); out.print('\t');
    out.print(tag.picture.offset); out.print('\t');
    out.print(tag.picture.length); out.print('\t');
    out.print(tag.picture.unsync ? 1 : 0); out.print('\t');
    out.print(tag.picture.type); out.print('\n');
    if (progress) progress(i + 1, tracks.size());
  }
  const bool written = out.getWriteError() == 0;
  out.close();
  if (!written) { SD.remove(INDEX_TMP); return false; }
  // Recoverable replace: preserve the previous generation until rename succeeds.
  SD.remove("/.sys/library.old");
  if (SD.exists(INDEX_PATH) && !SD.rename(INDEX_PATH, "/.sys/library.old")) return false;
  if (!SD.rename(INDEX_TMP, INDEX_PATH)) {
    SD.rename("/.sys/library.old", INDEX_PATH); return false;
  }
  SD.remove("/.sys/library.old");
  Serial.printf("library: indexed %u tracks (%d with ID3 tags) in %lu ms\n",
                (unsigned)tracks.size(), tagged, millis() - t0);
  return true;
}

// One pass over the index: record where each line starts.
static bool loadOffsets(int expected) {
  free(offsets);
  offsets = (uint32_t*)malloc(max(1, expected) * sizeof(uint32_t));
  if (!offsets) return false;
  File f = SD.open(INDEX_PATH);
  if (!f) return false;

  static uint8_t buf[1024];
  uint32_t filePos = 0;
  bool headerDone = false;
  int n = 0;
  for (;;) {
    const int got = f.read(buf, sizeof(buf));
    if (got <= 0) break;
    for (int i = 0; i < got; i++) {
      if (buf[i] != '\n') continue;
      const uint32_t next = filePos + i + 1;
      if (!headerDone) headerDone = true;
      else n++;
      if (n < expected && next < f.size()) offsets[n] = next;
    }
    filePos += got;
  }
  f.close();
  // The first entry starts right after the header line.
  trackCount = min(n, expected);
  return trackCount == expected;
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
bool storage_mount() {
  if (!mutex) mutex = xSemaphoreCreateMutex();
  sdSPI.begin(CYD_SD_SCK_PIN, CYD_SD_MISO_PIN, CYD_SD_MOSI_PIN, CYD_SD_CS_PIN);
  // max_files 4: the playing track, the background, a title strip or index
  // read, one spare. Each costs heap only while actually open.
  bool ok = SD.begin(CYD_SD_CS_PIN, sdSPI, 20000000, "/sd", 4);
  if (!ok) {
    SD.end();
    ok = SD.begin(CYD_SD_CS_PIN, sdSPI, 4000000, "/sd", 4);
  }
  if (!ok) {
    Serial.println("sd: mount failed");
    return false;
  }
  for (const char* dir : { "/music", "/bg", "/playlists", "/.sys" })
    if (!SD.exists(dir)) SD.mkdir(dir);
  return true;
}

bool storage_begin(StorageProgressFn progress) {
  if (!storage_mount()) return false;
  if (!SD.exists(INDEX_PATH) && SD.exists("/.sys/library.old"))
    SD.rename("/.sys/library.old", INDEX_PATH);

  // Walk /music. Runs before Bluetooth, so a temporary list is affordable.
  const uint32_t t0 = millis();
  std::vector<Found> tracks;
  scanDir("/music", 0, tracks);
  std::sort(tracks.begin(), tracks.end(),
            [](const Found& a, const Found& b) { return a.path < b.path; });
  uint32_t crc = 0;
  for (const Found& t : tracks) {
    crc = esp_rom_crc32_le(crc, (const uint8_t*)t.path.c_str(), t.path.length());
    crc = esp_rom_crc32_le(crc, (const uint8_t*)&t.size, sizeof(t.size));
    crc = esp_rom_crc32_le(crc, (const uint8_t*)&t.modified, sizeof(t.modified));
  }
  Serial.printf("sd: %u MB card, %u tracks (scan %lu ms)\n",
                (unsigned)(SD.cardSize() / (1024ULL * 1024ULL)), (unsigned)tracks.size(),
                millis() - t0);

  if (!indexIsCurrent(tracks.size(), crc) && !buildIndex(tracks, crc, progress)) {
    Serial.println("library: index upgrade failed; previous index preserved"); return false;
  }
  const int count = tracks.size();
  tracks.clear();
  tracks.shrink_to_fit();

  libraryCrc = crc;
  cacheIndex = -1;
  if (!loadOffsets(count)) {
    Serial.println("library: index unreadable -- no tracks");
    trackCount = 0;
  }
  return true;
}

int      storage_trackCount() { return trackCount; }
uint32_t storage_libraryCrc() { return libraryCrc; }

// Read entry i into the cache. Caller holds the mutex.
static bool fetch(int i) {
  if (i < 0 || i >= trackCount) return false;
  if (i == cacheIndex) return true;
  File f = SD.open(INDEX_PATH);
  if (!f) return false;
  f.seek(offsets[i]);
  const String line = f.readStringUntil('\n');
  f.close();
  splitInfo(line, cachePath, cacheInfo);
  cacheLabel = makeLabel(cachePath, cacheInfo.title, cacheInfo.artist);
  cacheIndex = i;
  return true;
}

String storage_trackPath(int i) {
  String out;
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (fetch(i)) out = cachePath;
  xSemaphoreGive(mutex);
  return out;
}

String storage_trackLabel(int i) {
  String out;
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (fetch(i)) out = cacheLabel;
  xSemaphoreGive(mutex);
  return out;
}

void storage_forEach(StorageEachFn fn) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  File f = SD.open(INDEX_PATH);
  if (f) {
    f.readStringUntil('\n');                   // header
    for (int i = 0; i < trackCount; i++) {
      const String line = f.readStringUntil('\n');
      String path, title, artist;
      splitLine(line, path, title, artist);
      fn(i, path, makeLabel(path, title, artist));
    }
    f.close();
  }
  xSemaphoreGive(mutex);
}

bool storage_trackInfo(int i, Id3Info& info) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  bool ok = fetch(i);
  info = ok ? cacheInfo : Id3Info();
  xSemaphoreGive(mutex);
  return ok;
}
void storage_forEachInfo(StorageInfoFn fn) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  File f = SD.open(INDEX_PATH);
  if (f) {
    f.readStringUntil('\n');
    for (int i = 0; i < trackCount; ++i) {
      const String line = f.readStringUntil('\n'); String path; Id3Info info;
      splitInfo(line, path, info); fn(i, path, info); yield();
    }
    f.close();
  }
  xSemaphoreGive(mutex);
}
