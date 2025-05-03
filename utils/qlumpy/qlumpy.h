/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
****/

#include "cmdlib.h"
#include "scriplib.h"
#include "lbmlib.h"
#include "wadlib.h"

// PVR format definitions
#define GBIXHEADER  (('X'<<24)+('I'<<16)+('B'<<8)+'G')  // "GBIX"
#define PVRTSIGN    (('T'<<24)+('R'<<16)+('V'<<8)+'P')  // "PVRT"

#define PVR_VQ              0x03
#define PVR_VQ_MIPMAP       0x04
#define PVR_SMALL_VQ        0x10
#define PVR_SMALL_VQ_MIPMAP 0x11
#define PVR_ARGB1555        0x00  // Color format
#define PVR_RGB565          0x01  // Color format

#pragma pack(push, 1)
typedef struct {
    uint32_t version;          // "GBIX"
    uint32_t nextTagOffset;    // Offset to next tag (PVRT)
    unsigned long long globalIndex;
} gbix_t;

typedef struct {
    uint32_t version;          // "PVRT"
    uint32_t textureDataSize;  // Size of rest of file
    uint8_t colorFormat;       // e.g., 0x00=ARGB1555, 0x01=RGB565
    uint8_t imageFormat;       // e.g., 0x03=VQ, 0x04=VQ_MIPMAP
    uint16_t zeroes;           // Always 0
    uint16_t width;
    uint16_t height;
} pvrt_t;
#pragma pack(pop)


extern  byte    *byteimage, *lbmpalette;
extern  int     byteimagewidth, byteimageheight;

#define SCRN(x,y)       (*(byteimage+(y)*byteimagewidth+x))

extern  byte    *lump_p;
extern  byte	*lumpbuffer;

extern	char	lumpname[];
#define MAXLUMP		0x50000         // biggest possible lump
