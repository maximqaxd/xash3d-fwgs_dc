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

#ifdef __GNUC__
#include <sys/stat.h>
#include <sys/types.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#endif
#include <errno.h>

#include "studio.h"
#include "activity.h"
#include "activitymap.h"

#ifdef _WIN32
#define SLASH '\\'
#else
#define SLASH '/'
#endif

void error (int code, const char *fmt, ...)
{
    va_list va;
    va_start (va, fmt);
    vfprintf (stderr, fmt, va);
    va_end (va);
    exit (code);
}

static bool isslash (char *c)
{
    return c[0] == '/'
        || c[0] == '\\';
}

void fixpath (char *str, bool lower)
{
    char *c = str;
    
    while (*c)
    {
        if (isslash (c))
            *c = SLASH;
        else if (lower)
            *c = tolower (*c);
        c++;
    }
}

char *skippath (char *str)
{
    char *c = str;
    char *c2 = str;

    while (*c)
    {
        if (isslash (c))
            c2 = c + 1;
        c++;
    }
    return c2;
}

static bool isext (char *c)
{
    return c[0] == '.'
        && c[1] != '/'
        && c[1] != '\\'
        && c[1] != '.'
        && c[1] != '\0';
}

void stripext (char *str)
{
    char *c = str;
    char *name = str;

    while (*c)
    {
        if (isslash (c))
            name = c + 1;
        c++;
    }

    c = name;

    while (*c)
    {
        if (isext (c))
        {
            *c = '\0';
            break;
        }
        c++;
    }
}

void stripfilename (char *str)
{
    char *c = str + strlen (str);
    bool foundext = false;

    while (c != str)
    {
        if (!foundext && isext (c))
            foundext = true;
        
        if (isslash (c))
        {
            if (!foundext)
                break;
            *(c + 1) = '\0';
            break;
        }
        c--;
    }
}

char *appenddir (char *path, char *dir)
{
    size_t path_len = strlen (path);
    size_t dir_len = strlen (dir);

    bool need_slash = !isslash (path + (path_len - 1));

    char *new_path = (char *)memalloc (path_len + need_slash + dir_len + 1, 1);
    strcpy (new_path, path);

    if (need_slash)
    {
        new_path[path_len] = SLASH;
        new_path[path_len + 1] = '\0';
    }
    
    strcat (new_path, dir);

    return new_path;
}

void filebase (char *str, char **name, char **ext)
{
    char *c = str;
    
    *name = str;
    *ext = str + strlen (str);

    while (*c)
    {
        if (isslash (c))
            *name = c + 1;
        c++;
    }

    c = *name;

    while (*c)
    {
        if (isext (c))
            *ext = c;
        c++;
    }
}

static bool makedir (const char *path)
{
#ifdef __GNUC__
    if (mkdir (path, 0777) != -1)
        return true;
#else
    if (_mkdir (path) != -1)
        return true;
#endif
    return errno == EEXIST;
}

bool makepath (const char *path)
{
    char *c = path;
    char slash;

    while (*c)
    {
        if (isslash (c))
        {
            slash = *c;
            *c = '\0';
            if (!makedir (path))
            {
                *c = slash;
                return false;
            }
            *c = slash;
        }
        c++;
    }

    return true;
}

void *memalloc (size_t nmemb, size_t size)
{
    void *ptr = calloc (nmemb, size);

    if (!ptr)
        error (1, "Failed to allocate %i bytes\n", nmemb * size);
    
    return ptr;
}

FILE *mdl_open (const char *filename, int *identifier, int *version, int safe)
{
    if (!safe)
        fprintf (stdout, "Reading from \"%s\"...\n", filename);

    FILE *stream = fopen (filename, "rb");

    if (!stream)
    {
        if (safe)
            return NULL;
        error (1, "No input file\n");
    }

    if (safe)
        fprintf (stdout, "Reading from \"%s\"...\n", filename);
    
    if (identifier)
    {
        int32_t id;
        mdl_read (stream, &id, sizeof (id));
        *identifier = id;
    }

    if (version)
    {
        int32_t v;
        mdl_read (stream, &v, sizeof (v));
        *version = v;
    }
    
    mdl_seek (stream, 0, SEEK_SET);
    
    return stream;
}

void mdl_read (FILE *stream, void *dst, size_t size)
{
    if (fread (dst, 1, size, stream) < size)
        error (1, "Read failed\n");
}

void mdl_seek (FILE *stream, long off, int whence)
{
    if (fseek (stream, off, whence) < 0)
        error (1, "Seek failed\n");
}

char* mdl_getmotionflag (int type)
{
    switch (type & STUDIO_TYPES)
    {
    case STUDIO_X: return "X";
    case STUDIO_Y: return "Y";
    case STUDIO_Z: return "Z";
    case STUDIO_XR: return "XR";
    case STUDIO_YR: return "YR";
    case STUDIO_ZR: return "ZR";
    case STUDIO_LX: return "LX";
    case STUDIO_LY: return "LY";
    case STUDIO_LZ: return "LZ";
    case STUDIO_AX: return "AX";
    case STUDIO_AY: return "AY";
    case STUDIO_AZ: return "AZ";
    case STUDIO_AXR: return "AXR";
    case STUDIO_AYR: return "AYR";
    case STUDIO_AZR: return "AZR";
    }
    return "";
}

char *mdl_getactname (int type)
{
    static char custom[64];
    int i;

    for (i = 0; i < (int)(sizeof(activity_map) / sizeof(activity_map[0])); ++i)
    {
        if (activity_map[i].type == type)
            return activity_map[i].name;
    }

    snprintf (custom, sizeof(custom), "ACT_%i", type);
    return custom;
}

void qc_makepath (const char *filename)
{
    char *name, *ext;
    filebase (filename, &name, &ext);

    if (name != filename)
    {
        char *path = strdup (filename);
        path[name - filename] = '\0';
        if (!makepath (path))
        {
            error (1, "Failed to make directory: \"%s\"\n", path);
        }
        free (path);
    }
}

FILE *qc_open (const char *filepath, const char *filename, const char *ext, bool binary)
{
    size_t len = strlen (filepath) + strlen (filename) + 8;

    if (ext)
        len += strlen (ext);
    
    char *fullname = (char *)memalloc (len, 1);
    
    strcpy (fullname, filepath);
    fixpath (fullname, false);
    
    len = strlen (fullname);

    if (!isslash (fullname + (len - 1)))
    {
        fullname[len] = SLASH;
        fullname[len + 1] = '\0';
    }
    
    strcat (fullname, filename);

    if (ext)
    {
        strcat (fullname, ".");
        strcat (fullname, ext);
    }

    qc_makepath (fullname);

    fprintf (stdout, "Writing to \"%s\"...\n", fullname);

#ifdef _WIN32
    FILE *stream = fopen (fullname, binary ? "wb" : "w");
#else
    FILE *stream = fopen (fullname, "w");
#endif

    if (!stream)
        error (1, "Failed to create file\n");

    free (fullname);

    return stream;
}

void qc_putc (FILE *stream, char c)
{
    if (fputc (c, stream) < 0)
        error (1, "Write failed\n");
}

void qc_write (FILE *stream, const char *str)
{
    size_t len = strlen (str);
    if (fwrite (str, 1, len, stream) < len)
        error (1, "Write failed\n");
    qc_putc (stream, '\n');
}

void qc_writef (FILE *stream, const char *fmt, ...)
{
    va_list va;
    va_start (va, fmt);
    if (vfprintf (stream, fmt, va) < 0)
        error (1, "Write failed\n");
    va_end (va);
    qc_putc (stream, '\n');
}

void qc_write2f (FILE *stream, const char *fmt, ...)
{
    va_list va;
    va_start (va, fmt);
    if (vfprintf (stream, fmt, va) < 0)
        error (1, "Write failed\n");
    va_end (va);
}

void qc_writeb (FILE *stream, void *ptr, size_t size)
{
    if (fwrite (ptr, 1, size, stream) < size)
        error (1, "Write failed\n");
}
