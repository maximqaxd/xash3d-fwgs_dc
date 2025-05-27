/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
****/

#ifndef _BSPFILE_H
#define _BSPFILE_H

// upper design bounds

#define MAX_MAP_HULLS 4

#define MAX_MAP_MODELS 400
#define MAX_MAP_BRUSHES 4096
#define MAX_MAP_ENTITIES 1024
#define MAX_MAP_ENTSTRING (128 * 1024)

#define MAX_MAP_PLANES 32767
#define MAX_MAP_NODES 32767		// because negative shorts are contents
#define MAX_MAP_CLIPNODES 32767 //
#define MAX_MAP_LEAFS 8192
#define MAX_MAP_VERTS 65535
#define MAX_MAP_FACES 65535
#define MAX_MAP_MARKSURFACES 65535
#define MAX_MAP_TEXINFO 8192
#define MAX_MAP_EDGES 256000
#define MAX_MAP_SURFEDGES 512000
#define MAX_MAP_TEXTURES 512
#define MAX_MAP_MIPTEX 0x200000
#define MAX_MAP_LIGHTING 0x200000
#define MAX_MAP_VISIBILITY 0x200000

#define MAX_MAP_PORTALS 65536

// key / value pair sizes

#define MAX_KEY 32
#define MAX_VALUE 1024

//=============================================================================


#define BSPVERSION 30
#define TOOLVERSION 2


typedef struct
{
	int32_t fileofs, filelen;
} lump_t;

#define LUMP_ENTITIES 0
#define LUMP_PLANES 1
#define LUMP_TEXTURES 2
#define LUMP_VERTEXES 3
#define LUMP_VISIBILITY 4
#define LUMP_NODES 5
#define LUMP_TEXINFO 6
#define LUMP_FACES 7
#define LUMP_LIGHTING 8
#define LUMP_CLIPNODES 9
#define LUMP_LEAFS 10
#define LUMP_MARKSURFACES 11
#define LUMP_EDGES 12
#define LUMP_SURFEDGES 13
#define LUMP_MODELS 14

#define HEADER_LUMPS 15

typedef struct
{
	float mins[3], maxs[3];
	float origin[3];
	int32_t headnode[MAX_MAP_HULLS];
	int32_t visleafs; // not including the solid leaf 0
	int32_t firstface, numfaces;
} dmodel_t;

typedef struct
{
	int32_t version;
	lump_t lumps[HEADER_LUMPS];
} dheader_t;

typedef struct
{
	int32_t nummiptex;
	int32_t dataofs[4]; // [nummiptex]
} dmiptexlump_t;


typedef struct
{
	float point32_t[3];
} dvertex_t;


// 0-2 are axial planes
#define PLANE_X 0
#define PLANE_Y 1
#define PLANE_Z 2

// 3-5 are non-axial planes snapped to the nearest
#define PLANE_ANYX 3
#define PLANE_ANYY 4
#define PLANE_ANYZ 5

typedef struct
{
	float normal[3];
	float dist;
	int32_t type; // PLANE_X - PLANE_ANYZ ?remove? trivial to regenerate
} dplane_t;



#define CONTENTS_EMPTY -1
#define CONTENTS_SOLID -2
#define CONTENTS_WATER -3
#define CONTENTS_SLIME -4
#define CONTENTS_LAVA -5
#define CONTENTS_SKY -6
#define CONTENTS_ORIGIN -7 // removed at csg time
#define CONTENTS_CLIP -8   // changed to contents_solid

#define CONTENTS_CURRENT_0 -9
#define CONTENTS_CURRENT_90 -10
#define CONTENTS_CURRENT_180 -11
#define CONTENTS_CURRENT_270 -12
#define CONTENTS_CURRENT_UP -13
#define CONTENTS_CURRENT_DOWN -14

#define CONTENTS_TRANSLUCENT -15

// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct
{
	int32_t planenum;
	short children[2]; // negative numbers are -(leafs+1), not nodes
	short mins[3];	   // for sphere culling
	short maxs[3];
	unsigned short firstface;
	unsigned short numfaces; // counting both sides
} dnode_t;

typedef struct
{
	int32_t planenum;
	short children[2]; // negative numbers are contents
} dclipnode_t;


typedef struct texinfo_s
{
	float vecs[2][4]; // [s/t][xyz offset]
	int32_t miptex;
	int32_t flags;
} texinfo_t;
#define TEX_SPECIAL 1 // sky or slime, no lightmap or 256 subdivision

// note that edge 0 is never used, because negative edge nums are used for
// counterclockwise use of the edge in a face
typedef struct
{
	unsigned short v[2]; // vertex numbers
} dedge_t;

#define MAXLIGHTMAPS 4
typedef struct
{
	short planenum;
	short side;

	int32_t firstedge; // we must support > 64k edges
	short numedges;
	short texinfo;

	// lighting info
	unsigned char styles[MAXLIGHTMAPS];
	int32_t lightofs; // start of [numstyles*surfsize] samples
} dface_t;



#define AMBIENT_WATER 0
#define AMBIENT_SKY 1
#define AMBIENT_SLIME 2
#define AMBIENT_LAVA 3

#define NUM_AMBIENTS 4 // automatic ambient sounds

// leaf 0 is the generic CONTENTS_SOLID leaf, used for all solid areas
// all other leafs need visibility info
typedef struct
{
	int32_t contents;
	int32_t visofs; // -1 = no visibility info

	short mins[3]; // for frustum culling
	short maxs[3];

	unsigned short firstmarksurface;
	unsigned short nummarksurfaces;

	unsigned char ambient_level[NUM_AMBIENTS];
} dleaf_t;


//============================================================================

#endif /* _BSPFILE_H */
