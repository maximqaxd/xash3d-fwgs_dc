/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
****/

// wadlib.h

//
// wad reading
//

#define	CMP_NONE		0
#define	CMP_LZSS		1
#define CMP_VQ			2

#define	TYP_NONE		0
#define	TYP_LABEL		1
#define	TYP_LUMPY		64				// 64 + grab command number

#include <stdint.h>

typedef struct
{
	char		identification[4];		// should be WAD2 or 2DAW
	int			numlumps;
	int			infotableofs;
} wadinfo_t;


typedef struct
{
	int			filepos;
	int			disksize;
	int			size;					// uncompressed
	char		type;
	char		compression;
	char		pad1, pad2;
	char		name[16];				// must be null terminated
} lumpinfo_t;

extern	lumpinfo_t		*lumpinfo;		// location of each lump on disk
extern	int				numlumps;
extern	wadinfo_t		header;

void	W_OpenWad (char *filename);
int		W_CheckNumForName (char *name);
int		W_GetNumForName (char *name);
int		W_LumpLength (int lump);
void	W_ReadLumpNum (int lump, void *dest);
void	*W_LoadLumpNum (int lump);
void	*W_LoadLumpName (char *name);

void CleanupName (char *in, char *out);

//
// wad creation
//
void	NewWad (char *pathname, qboolean bigendien);
void	AddLump (char *name, void *buffer, int length, int type, int compress);
void	WriteWad (int wad3);

#define PVRTSIGN (('T'<<24)+('R'<<16)+('V'<<8)+'P') // little-endian "PVRT"

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

void LoadScreenPVR(char* name);