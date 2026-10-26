#include "id3.h"
#include "media_yield.h"

static const size_t MAX_TEXT = 200;          // bytes of frame body worth reading

static uint32_t syncsafe(const uint8_t* p) {
  return ((p[0] & 0x7F) << 21) | ((p[1] & 0x7F) << 14) | ((p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

static void appendUtf8(String& s, uint32_t cp) {
  if (cp == 0) return;
  char b[5] = { 0 };
  if (cp < 0x80)        { b[0] = cp; }
  else if (cp < 0x800)  { b[0] = 0xC0 | (cp >> 6);  b[1] = 0x80 | (cp & 0x3F); }
  else if (cp < 0x10000){ b[0] = 0xE0 | (cp >> 12); b[1] = 0x80 | ((cp >> 6) & 0x3F);
                          b[2] = 0x80 | (cp & 0x3F); }
  else                  { b[0] = 0xF0 | (cp >> 18); b[1] = 0x80 | ((cp >> 12) & 0x3F);
                          b[2] = 0x80 | ((cp >> 6) & 0x3F); b[3] = 0x80 | (cp & 0x3F); }
  s += b;
}

// Text frame body: one encoding byte, then the string (maybe NUL-terminated,
// maybe several NUL-separated values in v2.4 -- the first is kept).
static String decodeText(const uint8_t* p, size_t n) {
  String out;
  if (n < 2) return out;
  const uint8_t enc = p[0];
  p++; n--;
  if (enc == 0) {                              // ISO-8859-1
    for (size_t i = 0; i < n && p[i]; i++) appendUtf8(out, p[i]);
  } else if (enc == 3) {                       // UTF-8
    for (size_t i = 0; i < n && p[i]; i++) out += (char)p[i];
  } else if (enc == 1 || enc == 2) {             // UTF-16 with BOM or BE
    bool be = (enc == 2);
    size_t i = 0;
    if (enc == 1 && n >= 2) {
      if (p[0] == 0xFE && p[1] == 0xFF) { be = true;  i = 2; }
      else if (p[0] == 0xFF && p[1] == 0xFE) { be = false; i = 2; }
    }
    for (; i + 1 < n; i += 2) {
      uint32_t u = be ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
      if (u == 0) break;
      if (u >= 0xD800 && u < 0xDC00 && i + 3 < n) {         // surrogate pair
        const uint32_t lo = be ? (p[i + 2] << 8 | p[i + 3]) : (p[i + 3] << 8 | p[i + 2]);
        if (lo >= 0xDC00 && lo <= 0xDFFF) {
          u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00); i += 2;
        } else u = 0xFFFD;
      }
      if (u >= 0xD800 && u <= 0xDFFF) u = 0xFFFD;
      appendUtf8(out, u);
    }
  }
  out.trim();
  // A tab or newline in a title would break the library index's line format.
  out.replace('\t', ' ');
  out.replace('\n', ' ');
  out.replace('\r', ' ');
  return out;
}


namespace {
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
// Buffered raw reads retain physical positions while optionally de-unsynchronising.
class TagReader {
 public:
  fs::File& file;
  uint32_t pos, end;
  bool unsync, failed = false;
  TagReader(fs::File& f, uint32_t start, uint32_t limit, bool u) : file(f), pos(start), end(limit), unsync(u) {}
  int get() {
    int c;
    do {
      if (pos >= end) return -1;
      if (pos < from || pos >= from + have) {
        if (!file.seek(pos)) { failed = true; return -1; }
        from = pos; have = file.read(buffer, min<uint32_t>(sizeof(buffer), end - pos));
        if (!have) { failed = true; return -1; }
      }
      c = buffer[pos++ - from];
      if (unsync && previousFF && c == 0) { previousFF = false; continue; }
      previousFF = c == 255; return c;
    } while (true);
  }
  bool read(uint8_t* p, uint32_t n) { for (uint32_t i=0;i<n;++i) { int c=get(); if(c<0)return false; p[i]=c; } return true; }
  bool skip(uint32_t n) {
    if (!unsync) { if(n>end-pos)return false;pos+=n;return true; }
    for (uint32_t i=0;i<n;++i) { if(get()<0)return false;if((i&1023)==0)media_yield(pause); } return true;
  }
 private:
  uint8_t buffer[512]; uint32_t from = UINT32_MAX, have = 0, pause = 0; bool previousFF = false;
};
bool safeSync(const uint8_t* p) { return !((p[0]|p[1]|p[2]|p[3])&128); }
bool frameName(const uint8_t* p,int n) {
  for(int i=0;i<n;++i) if(!((p[i]>='A'&&p[i]<='Z')||(p[i]>='0'&&p[i]<='9')))return false;
  return true;
}
void picture(TagReader& body, bool legacy, Id3Info& out) {
  const int encoding=body.get(); if(encoding<0||encoding>3)return;
  char mime[65]={}; unsigned len=0;
  if(legacy) { for(int i=0;i<3;++i){int c=body.get();if(c<0)return;mime[len++]=c;} }
  else { int c; while((c=body.get())>0) { if(len>=64)return;mime[len++]=c; } if(c<0)return; }
  const int type=body.get(); if(type<0)return;
  bool terminated=false;
  for(unsigned i=0;i<4096;) {
    int a=body.get(); if(a<0)return; ++i;
    if(encoding==1||encoding==2) {int b=body.get();if(b<0)return;++i;if(a==0&&b==0){terminated=true;break;}}
    else if(a==0){terminated=true;break;}
  }
  if(!terminated || !(strcmp(mime,"image/jpeg")==0 || strcmp(mime,"image/jpg")==0 || strcmp(mime,"JPG")==0))return;
  const uint32_t start=body.pos, length=body.end-start;
  if(length<3||length>4*1024*1024)return;
  if(body.get()!=255||body.get()!=216||body.get()!=255)return;
  if(!out.picture.length || (type==3 && out.picture.type!=3)) {
    out.picture.offset=start;out.picture.length=length;out.picture.unsync=body.unsync;out.picture.type=type;
  }
}
}
bool id3_read(fs::File& f, Id3Info& out) {
  out = Id3Info(); uint8_t h[10];
  if(!f.seek(0)||f.read(h,10)!=10||memcmp(h,"ID3",3)||!safeSync(h+6))return false;
  const uint8_t version=h[3];
  if(version<2||version>4||(version==2&&(h[5]&0x40)))return false;
  const uint32_t length=syncsafe(h+6);
  if(length>16*1024*1024 || uint64_t(length)+10>f.size())return false;
  uint32_t pause = millis();
  const bool globalUnsync=version<4&&(h[5]&0x80);
  TagReader tag(f,10,10+length,globalUnsync);
  if(version>=3&&(h[5]&0x40)) {
    uint8_t e[4];if(!tag.read(e,4))return false;
    if(version==4&&!safeSync(e))return false;
    uint32_t size=version==4?syncsafe(e):be32(e);
    if((version==4&&size<6)||(version==3&&size<6))return false;
    if(!tag.skip(version==4?size-4:size))return false;
  }
  for(unsigned frames=0;frames<4096 && tag.pos<tag.end;++frames) {
    uint8_t fh[10]={}; const int hdr=version==2?6:10;
    if(!tag.read(fh,hdr)||!fh[0])break;
    if(!frameName(fh,version==2?3:4)||(version==4&&!safeSync(fh+4)))break;
    uint32_t size=version==2?(uint32_t(fh[3])<<16|uint32_t(fh[4])<<8|fh[5]):version==4?syncsafe(fh+4):be32(fh+4);
    if(!size)break;
    const uint32_t start=tag.pos;
    if(!tag.skip(size))break; // v2.3 sizes are logical under tag-wide unsync
    TagReader body(f,start,tag.pos,globalUnsync || (version==4&&((h[5]&0x80)||(fh[9]&2))));
    bool unsupported=version==3?(fh[9]&0xC0):version==4?(fh[9]&0x0C):false;
    if(unsupported)continue;
    if((version==3&&(fh[9]&0x20))||(version==4&&(fh[9]&0x40)))if(!body.skip(1))continue;
    if(version==4&&(fh[9]&1)) {uint8_t dli[4];if(!body.read(dli,4)||!safeSync(dli))continue;}
    String* target=nullptr;
    if(version==2) {
      if(!memcmp(fh,"TT2",3))target=&out.title;
      else if(!memcmp(fh,"TP1",3))target=&out.artist;
      else if(!memcmp(fh,"TAL",3))target=&out.album;
      else if(!memcmp(fh,"TRK",3))target=&out.track;
      else if(!memcmp(fh,"PIC",3))picture(body,true,out);
    } else {
      if(!memcmp(fh,"TIT2",4))target=&out.title;
      else if(!memcmp(fh,"TPE1",4))target=&out.artist;
      else if(!memcmp(fh,"TALB",4))target=&out.album;
      else if(!memcmp(fh,"TRCK",4))target=&out.track;
      else if(!memcmp(fh,"APIC",4))picture(body,false,out);
    }
    if(target&&target->isEmpty()) {
      uint8_t text[MAX_TEXT];size_t n=0;int c;
      while(n<sizeof(text)&&(c=body.get())>=0)text[n++]=c;
      if(!body.failed)*target=decodeText(text,n);
    }
    if((frames&31)==0)media_yield(pause);
  }
  return out.title.length()||out.artist.length()||out.album.length()||out.track.length()||out.picture.length;
}
bool id3_extractJpeg(fs::File& mp3,const Id3Picture& picture,fs::File& output) {
  if(!mp3||!output||picture.length<3||picture.length>4*1024*1024||uint64_t(picture.offset)+picture.length>mp3.size())return false;
  TagReader signature(mp3,picture.offset,picture.offset+picture.length,picture.unsync);
  if(signature.get()!=255||signature.get()!=216||signature.get()!=255)return false;
  TagReader reader(mp3,picture.offset,picture.offset+picture.length,picture.unsync);
  uint8_t buffer[512];size_t count=0;uint32_t pause=millis();
  for(;;) {
    int c=reader.get();if(c<0)break;
    buffer[count++]=c;
    if(count==sizeof(buffer)){if(output.write(buffer,count)!=count)return false;count=0;media_yield(pause);}
  }
  return !reader.failed && (!count||output.write(buffer,count)==count);
}
