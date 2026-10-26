#pragma once
#include <cstdint>
#include <cstddef>
inline uint32_t esp_rom_crc32_le(uint32_t seed,const uint8_t* p,size_t n){
 uint32_t c=~seed;while(n--){c^=*p++;for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320U&uint32_t(-int(c&1)));}return ~c;
}
