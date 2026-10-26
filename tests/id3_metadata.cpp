#include <cassert>
#include <fstream>
#include <vector>
#include "id3.h"
#include "SD.h"
using Bytes=std::vector<uint8_t>;
static void word(Bytes& b,uint32_t n,bool sync=false){for(int s=sync?21:24;s>=0;s-=sync?7:8)b.push_back((n>>s)&(sync?127:255));}
static Bytes text(const char* s){Bytes b{3};b.insert(b.end(),s,s+strlen(s));return b;}
static void frame(Bytes& tag,const char* id,const Bytes& body,int version=3,uint8_t flags=0){
 tag.insert(tag.end(),id,id+(version==2?3:4));
 if(version==2){tag.push_back(body.size()>>16);tag.push_back(body.size()>>8);tag.push_back(body.size());}
 else {word(tag,body.size(),version==4);tag.push_back(0);tag.push_back(flags);}
 tag.insert(tag.end(),body.begin(),body.end());
}
static Bytes unsync(const Bytes& raw){Bytes b;for(auto c:raw){b.push_back(c);if(c==255)b.push_back(0);}return b;}
static Bytes apic(const Bytes& image,int type=3,bool legacy=false){
 Bytes b{0};const char* mime=legacy?"JPG":"image/jpeg";b.insert(b.end(),mime,mime+strlen(mime));
 if(!legacy) b.push_back(0);
 b.push_back(type);b.push_back(0);b.insert(b.end(),image.begin(),image.end());return b;
}
static void saveTag(const Bytes& body,int version=3,uint8_t flags=0){
 Bytes h{'I','D','3',uint8_t(version),0,flags};word(h,body.size(),true);h.insert(h.end(),body.begin(),body.end());
 std::ofstream f(SD.root+"/song.mp3",std::ios::binary);f.write((char*)h.data(),h.size());
}
static void extract(const Id3Info& info,const Bytes& expected){
 auto input=SD.open("/song.mp3"),output=SD.open("/cover.jpg",FILE_WRITE);
 assert(id3_extractJpeg(input,info.picture,output));output.close();
 std::ifstream f(SD.root+"/cover.jpg",std::ios::binary);Bytes got((std::istreambuf_iterator<char>(f)),{});assert(got==expected);
}
int main(int argc,char** argv){
 assert(argc==2);SD.root=argv[1];
 Bytes jpeg{255,216,255,224};jpeg.insert(jpeg.end(),7000,255);jpeg.push_back(217);
 Bytes tag;frame(tag,"TIT2",text("Song"));frame(tag,"TPE1",text("Artist"));frame(tag,"TALB",text("Album"));frame(tag,"TRCK",text("3/12"));
 frame(tag,"APIC",apic(Bytes{255,216,255,0,217},0));frame(tag,"APIC",apic(jpeg));
 saveTag(tag);Id3Info info;auto f=SD.open("/song.mp3");assert(id3_read(f,info));f.close();
 assert(info.title=="Song"&&info.artist=="Artist"&&info.album=="Album"&&info.track=="3/12"&&info.picture.type==3);extract(info,jpeg);
 saveTag(unsync(tag),3,128);f=SD.open("/song.mp3");assert(id3_read(f,info));f.close();assert(info.title=="Song"&&info.picture.unsync);extract(info,jpeg);
 tag.clear();frame(tag,"TIT2",Bytes{1,255,254,'A',0,0x2d,0x4e,0,0},4);frame(tag,"APIC",unsync(apic(jpeg)),4,2);
 saveTag(tag,4);f=SD.open("/song.mp3");assert(id3_read(f,info));f.close();assert(info.title=="A中");extract(info,jpeg);
 tag.clear();frame(tag,"TT2",text("Old"),2);frame(tag,"TRK",text("7"),2);frame(tag,"PIC",apic(jpeg,3,true),2);
 saveTag(tag,2);f=SD.open("/song.mp3");assert(id3_read(f,info));f.close();assert(info.track=="7");extract(info,jpeg);
 tag.clear();frame(tag,"TIT2",text("Must skip"),4,8);frame(tag,"TPE1",Bytes{0,'B',0xe9},4);saveTag(tag,4);
 f=SD.open("/song.mp3");assert(id3_read(f,info));f.close();assert(info.title.empty()&&info.artist=="Bé");
 // Correct extended-header sizing differs between v2.3 (exclusive) and v2.4 (inclusive).
 for(int v:{3,4}){tag.clear();word(tag, v==3?6:6,v==4);tag.insert(tag.end(),v==3?6:2,0);frame(tag,"TIT2",text("Extended"),v);saveTag(tag,v,64);f=SD.open("/song.mp3");assert(id3_read(f,info)&&info.title=="Extended");f.close();}
 tag.clear();frame(tag,"TIT2",text("Safe"));saveTag(tag);std::filesystem::resize_file(SD.root+"/song.mp3",11);
 f=SD.open("/song.mp3");assert(!id3_read(f,info));f.close();
 puts("PASS: ID3 v2.2/3/4 metadata, UTF16/Latin1, front APIC after text, unsync extraction, extended headers, compression/truncation");
}
