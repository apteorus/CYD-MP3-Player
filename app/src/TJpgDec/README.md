# Pinned JPEG decoder core

Unmodified `tjpgd.c`, `tjpgd.h`, `tjpgdcnf.h` from Bodmer's Arduino
**TJpg_Decoder 1.1.0** package (TJpgDec R0.03, ChaN 2021):
https://github.com/Bodmer/TJpg_Decoder

The source copyright/license comments and package `license.txt` (LICENSE.txt
here) are preserved. Only the C decoder is bundled. Arduino's library linking
otherwise retains the wrapper's global TJpgDec object and ~3.5 KB workspace even
when using the C API; omitting that wrapper lets ChonkPod allocate the 3,500-byte
workspace only during boot conversion and release it before Bluetooth startup.
No extra JPEG library installation or custom Arduino build flags are required.

Configuration remains upstream RGB565 output, 512-byte input, scale support,
JD_FASTDECODE=1. jpeg_cache.cpp supplies checked SD callbacks and bounded MCU-to-row
scaling. Progressive JPEGs are not supported; conversion failures use the folder
cover or placeholder. Review core/configuration changes with the production
JPEG/cache host tests before upgrading this pinned code.
