#include "artwork.h"
#include "cover_cache.h"
#include <cassert>
#include <fstream>
static unsigned fingerprint=123, scans=0, prepared=0, failures=0;
static bool readable=true;
int storage_trackCount(){return 2;}
uint32_t storage_artworkCrc(){return fingerprint;}
bool cover_prepare(const String&,const Id3Info&){++prepared;return !failures;}
bool storage_forEachInfo(StorageInfoFn fn){
 ++scans;if(!readable)return false;
 Id3Info info;fn(0,"/music/a.mp3",info);fn(1,"/music/b.mp3",info);return true;
}
static void write(const char* path,const char* text){File f=SD.open(path,FILE_WRITE);assert(f);assert(f.print(text)==strlen(text));}
int main(int argc,char**argv){
 assert(argc==2);SD.root=argv[1];SD.mkdir("/.sys");
 art_prepareAll(nullptr);assert(scans==1&&prepared==2&&SD.exists("/.sys/art/prepared"));
 art_prepareAll(nullptr);assert(scans==1&&prepared==2); // zero track traversal
 ++fingerprint;art_prepareAll(nullptr);assert(scans==2&&prepared==4);
 {File f=SD.open("/.sys/art/prepared");uint8_t b[16];assert(f.read(b,16)==16);f.close();b[0]='2';
  SD.remove("/.sys/art/prepared");f=SD.open("/.sys/art/prepared",FILE_WRITE);assert(f.write(b,16)==16);}
 art_prepareAll(nullptr);assert(scans==3); // wrong version rejected
 SD.remove("/.sys/art/prepared");write("/.sys/art/prepared","ACP1");
 art_prepareAll(nullptr);assert(scans==4); // truncated checkpoint rejected
 ++fingerprint;readable=false;art_prepareAll(nullptr);assert(!SD.exists("/.sys/art/prepared"));
 readable=true;failures=1;art_prepareAll(nullptr);assert(!SD.exists("/.sys/art/prepared"));
 failures=0;art_prepareAll(nullptr);assert(SD.exists("/.sys/art/prepared"));
 write("/.sys/art/a.ref","ref");write("/.sys/art/a.ref.busy","busy");
 write("/.sys/art/a.rgb.bad","bad");write("/.sys/art/keep.rgb","rgb");
 assert(art_requestRefresh());unsigned before=scans;
 art_prepareAll(nullptr);assert(scans==before+1);
 assert(!SD.exists("/.sys/art/refresh")&&!SD.exists("/.sys/art/a.ref")&&!SD.exists("/.sys/art/a.ref.busy")&&!SD.exists("/.sys/art/a.rgb.bad"));
 assert(SD.exists("/.sys/art/keep.rgb"));art_prepareAll(nullptr);assert(scans==before+1);
 // A failed checkpoint replacement must not authorize the next boot to skip.
 ++fingerprint;fs::renameCalls=0;fs::failRenameAt=0;art_prepareAll(nullptr);fs::failRenameAt=-1;
 assert(!SD.exists("/.sys/art/prepared"));before=scans;art_prepareAll(nullptr);assert(scans==before+1);
 assert(fs::openFiles==0);
 puts("PASS: checkpoint zero-scan reboot, changes/version/truncation/I/O, manual recovery and failed commit");
}
