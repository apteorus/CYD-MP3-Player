#pragma once
#include "Arduino.h"
class TFT_eSPI {
 public:
 uint16_t alphaBlend(uint8_t,uint16_t,uint16_t){return 0;}
 void startWrite(){} void endWrite(){}
 void pushImage(int,int,int,int,uint16_t*){}
};
