/*
===========================================================================
Copyright (C) 1996-2002, Valve LLC. All rights reserved.
Copyright (C) 2023 Toodles

This product contains software technology licensed from Id 
Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
All Rights Reserved.

Use, distribution, and modification of this source code and/or resulting
object code is restricted to non-commercial enhancements to products from
Valve LLC.  All other use, distribution, or modification is prohibited
without written permission from Valve LLC.
===========================================================================
*/

#ifndef _DECOMPILE_H
#define _DECOMPILE_H

#define STUDIO_VERSION 10

#define IDSTUDIOHEADER (('T' << 24) + ('S' << 16) + ('D' << 8) + 'I')
// little-endian "IDST"
#define IDSTUDIOSEQHEADER (('Q' << 24) + ('S' << 16) + ('D' << 8) + 'I')
// little-endian "IDSQ"

#define SPRITE_VERSION 2

#define IDSPRITEHEADER (('P' << 24) + ('S' << 16) + ('D' << 8) + 'I')
// little-endian "IDSP"

#define IDWADHEADER (('3' << 24) + ('D' << 16) + ('A' << 8) + 'W')
// little-endian "WAD3"

enum {
	PITCH,
	YAW,
	ROLL,
};

typedef unsigned char byte;

typedef float vec_t;
typedef vec_t vec2_t[2];
typedef vec_t vec3_t[3];
typedef vec_t vec4_t[4];
typedef vec4_t mat4x3_t[3];

typedef struct
{
	byte r, g, b;
} rgb_t;

typedef struct
{
	byte b, g, r, x;
} rgb2_t;

#define vec3_print(v) v[0], v[1], v[2]
#define vec3_nonzero(v) v[0] || v[1] || v[2]
#define bone_print(v) v[0], v[1], v[2], v[3], v[4], v[5]

void error (int code, const char *fmt, ...);

void fixpath (char *str, bool lower);
char *skippath (char *str);
void stripext (char *str);
void stripfilename (char *str);
char *appenddir (char *path, char *dir);
void filebase (char *str, char **name, char **ext);
bool makepath (const char *path);

void *memalloc (size_t nmemb, size_t size);

#define	Q_PI 3.14159265358979323846F

#define dot(x, y) ((x)[0] * (y)[0] + (x)[1] * (y)[1] + (x)[2] * (y)[2])

void anglequaternion (const vec3_t angles, vec4_t quaternion);
void quaternionmatrix (const vec4_t quaternion, mat4x3_t matrix);
void concattransforms (const mat4x3_t in1, const mat4x3_t in2, mat4x3_t out);
void vectortransform (const vec3_t in1, const mat4x3_t in2, vec3_t out);
void vectorrotate (const vec3_t in1, const mat4x3_t in2, vec3_t out);

FILE *mdl_open (const char *filename, int *identifier, int *version, int safe);
void mdl_read (FILE *stream, void *dst, size_t size);
void mdl_seek (FILE *stream, long off, int whence);
char* mdl_getmotionflag (int type);
char *mdl_getactname (int type);

void qc_makepath (const char *filename);
FILE *qc_open (const char *filepath, const char *filename, const char *ext, bool binary);
void qc_putc (FILE *stream, char c);
void qc_write (FILE *stream, const char *str);
void qc_writef (FILE *stream, const char *fmt, ...);
void qc_write2f (FILE *stream, const char *fmt, ...);
void qc_writeb (FILE *stream, void *ptr, size_t size);

#endif /* _DECOMPILE_H */
