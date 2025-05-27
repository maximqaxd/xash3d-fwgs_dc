/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
****/

#ifndef _WADLIB_H
#define _WADLIB_H

// wadlib.h

//
// wad reading
//

#define CMP_NONE 0
#define CMP_LZSS 1

#define TYP_NONE 0
#define TYP_LABEL 1
#define TYP_LUMPY 64 // 64 + grab command number
#define TYP_PALETTE 64
#define TYP_COLORMAP 65
#define TYP_QPIC 66
#define TYP_MIPTEX 67
#define TYP_RAW 68
#define TYP_COLORMAP2 69
#define TYP_FONT 70

typedef struct
{
	int32_t id; // should be WAD2 or 2DAW
	int32_t numlumps;
	int32_t infotableofs;
} wadinfo_t;


typedef struct
{
	int32_t filepos;
	int32_t disksize;
	int32_t size; // uncompressed
	char type;
	char compression;
	char pad[2];
	char name[16]; // must be null terminated
} lumpinfo_t;

#define MIPLEVELS 4
typedef struct
{
	char name[16];
	uint32_t width, height;
	uint32_t offsets[MIPLEVELS]; // four mip maps stored
} miptex_t;

#endif /* _WADLIB_H */
