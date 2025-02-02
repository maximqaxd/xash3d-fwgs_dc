/*
img_pvr.h - pvr format reference
Copyright (C) 2024 maximqad

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef IMG_PVR_H
#define IMG_PVR_H
/*
========================================================================

.PVR image format

========================================================================
*/
#define GBIXHEADER	(('X'<<24)+('I'<<16)+('B'<<8)+'G') // little-endian "GBIX"
#define PVRTSIGN	(('T'<<24)+('R'<<16)+('V'<<8)+'P') // little-endian "PVRT"



#define PVR_TWIDDLE           0x01
#define PVR_TWIDDLED_MIPMAP   0x02
#define PVR_VQ                0x03
#define PVR_VQ_MIPMAP         0x04
#define PVR_CLUT8_TWIDDLED    0x05
#define PVR_CLUT4_TWIDDLED    0x06
#define PVR_DIRECT8_TWIDDLED  0x07
#define PVR_DIRECT4_TWIDDLED  0x08
#define PVR_RECT              0x09
#define PVR_RECTANGULAR_STRIDE 0x0B
#define PVR_RECTANGULAR_TWIDDLED 0x0D
#define PVR_SMALL_VQ          0x10
#define PVR_SMALL_VQ_MIPMAP   0x11
#define PVR_TWIDDLED_MIPMAP2  0x12

#define PVR_ARGB1555 0x00
#define PVR_RGB565   0x01
#define PVR_ARGB4444 0x02

#pragma pack(push,1)
typedef struct gbix_s
{
    uint32_t version;           // "GBIX" in ASCII
    uint32_t nextTagOffset;     // bytes number to next tag
    unsigned long long globalIndex;
} gbix_t;

typedef struct pvrt_s 
{
    uint32_t version;          // "PVRT" in ASCII
    uint32_t textureDataSize;  // Size of rest of the file
    uint8_t colorFormat;       // 0x01 for RGB565
    uint8_t imageFormat;       // 0x01=twiddled, 0x03=VQ, 0x09=rectangle
    uint16_t zeroes;          // Always 0
    uint16_t width;
    uint16_t height;
} pvrt_t;
#pragma pack(pop)


#endif // IMG_PVR_H

