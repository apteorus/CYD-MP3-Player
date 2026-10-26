#pragma once
#include "Arduino.h"
#include <memory>
#include <filesystem>
namespace fs {
class File {
 std::shared_ptr<FILE> handle;
 public:
 File()=default;
 explicit File(FILE* f):handle(f,[](FILE* p){if(p)fclose(p);}){}
 explicit operator bool() const {return handle!=nullptr;}
 int read(){return handle?fgetc(handle.get()):-1;}
 size_t read(uint8_t* p,size_t n){return handle?fread(p,1,n,handle.get()):0;}
 size_t write(const uint8_t* p,size_t n){return handle?fwrite(p,1,n,handle.get()):0;}
 size_t write(uint8_t b){return write(&b,1);}
 bool seek(uint32_t p){return handle&&fseek(handle.get(),p,SEEK_SET)==0;}
 uint32_t position(){return handle?ftell(handle.get()):0;}
 uint32_t size(){if(!handle)return 0;auto p=ftell(handle.get());fseek(handle.get(),0,SEEK_END);auto s=ftell(handle.get());fseek(handle.get(),p,SEEK_SET);return s;}
 void close(){handle.reset();}
};
class FS {
 public:
 std::string root;
 File open(const String& path,const char* mode="r"){return File(fopen((root+path).c_str(),mode));}
 bool exists(const String& p){return std::filesystem::exists(root+p);}
 bool remove(const String& p){return std::remove((root+p).c_str())==0;}
 bool rename(const String& a,const String& b){return std::rename((root+a).c_str(),(root+b).c_str())==0;}
 bool mkdir(const String& p){return std::filesystem::create_directories(root+p);}
};
}
using File=fs::File;
#define FILE_WRITE "w"
