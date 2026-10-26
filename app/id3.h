// ===========================================================================
// id3.h -- read title / artist / album from an MP3's ID3v2 tag
// ===========================================================================
// Handles ID3v2.2 (TT2/TP1/TAL), v2.3 and v2.4 (TIT2/TPE1/TALB), in all four
// text encodings (Latin-1, UTF-16 with BOM, UTF-16BE, UTF-8), always returning
// UTF-8. Pictures are represented as bounded file spans, never RAM blobs.
//
// ID3v1 is deliberately not read: Chinese files that only have a v1 tag almost
// always store it in GBK or Big5 with no marker saying which, and a wrong
// guess shows garbage where the file name would have been fine.
#pragma once

#include <Arduino.h>
#include <FS.h>

struct Id3Picture {
  uint32_t offset = 0, length = 0; // raw byte span inside the MP3, never picture data in RAM
  bool unsync = false;            // strip ID3 unsynchronisation while extracting
  uint8_t type = 0;               // APIC picture type, front cover (3) preferred
};
struct Id3Info {
  String title, artist, album, track; // TRCK text retains e.g. "3/12"
  Id3Picture picture;
};

// Bounded ID3v2.2/2.3/2.4 text and JPEG PIC/APIC discovery. Skips unsupported
// compressed/encrypted frames and non-JPEG images; parses beyond text frames.
bool id3_read(fs::File& f, Id3Info& out);
// Copy a discovered JPEG span to an already-open output in 512-byte chunks.
// Returns false for stale/truncated offsets or I/O errors. Caller removes temp on failure.
bool id3_extractJpeg(fs::File& mp3, const Id3Picture& picture, fs::File& output);
