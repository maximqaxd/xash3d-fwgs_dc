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

#include "wadlib.h"
#include "bspfile.h"

void decomp_writebmp (FILE *bmp, byte *data, int width, int height, byte *palette);

static void decomp_miptex (
    FILE *wad,
    const char *bmpdir,
    miptex_t *mip)
{
    byte *data = (byte *)memalloc (mip->width * mip->height + 768, 1);
    byte *palette = data + mip->width * mip->height;
    
    mdl_seek (wad, mip->offsets[0], SEEK_SET);
    mdl_read (wad, data, mip->width * mip->height);

    mdl_seek (
        wad,
        mip->offsets[0] + (mip->width * mip->height / 64 * 85) + sizeof(unsigned short),
        SEEK_SET
    );
    mdl_read (wad, palette, 768);

    FILE *bmp = qc_open (bmpdir, mip->name, "bmp", true);

    decomp_writebmp (bmp, data, mip->width, mip->height, palette);

    free (data);
    fclose (bmp);
}

void decomp_wad (
    const char *wadname,
    const char *bmpdir,
    const char *pattern)
{
    int id;
    FILE *wad = mdl_open (wadname, &id, NULL, false);

    if (id != IDWADHEADER)
        error (1, "Not a Valve WAD\n");

    wadinfo_t info;
    mdl_read (wad, &info, sizeof (info));

    int i, j;

    lumpinfo_t lumpinfo;
    miptex_t mip;

    if (pattern)
    {
        fixpath (pattern, true);
    }

    for (i = 0; i < info.numlumps; ++i)
    {
        mdl_seek (wad, info.infotableofs + sizeof (lumpinfo) * i, SEEK_SET);
        mdl_read (wad, &lumpinfo, sizeof (lumpinfo));

        switch (lumpinfo.type)
        {
        case TYP_MIPTEX:
            mdl_seek (wad, lumpinfo.filepos, SEEK_SET);
            mdl_read (wad, &mip, sizeof (mip));
            fixpath (mip.name, true);
            
            if (pattern && !strstr (mip.name, pattern))
            {
                break;
            }

            for (j = 0; j < MIPLEVELS; ++j)
            {
                mip.offsets[j] += lumpinfo.filepos;
            }
            
            decomp_miptex (wad, bmpdir, &mip);
            break;
        default:
            break;
        }
    }

    fclose (wad);

    fprintf (stdout, "Done!\n");
}

void decomp_bsptex (
    const char *bspname,
    const char *bmpdir,
    const char *pattern)
{
    int id;
    FILE *bsp = mdl_open (bspname, &id, NULL, false);

    if (id != BSPVERSION)
        fprintf (stderr, "Warning: Not a Valve BSP\n");

    dheader_t header;
    mdl_read (bsp, &header, sizeof (header));

    int i, j;

    int32_t nummiptex;
    int32_t dataofs;
    miptex_t mip;
    
    mdl_seek (bsp, header.lumps[LUMP_TEXTURES].fileofs, SEEK_SET);
    mdl_read (bsp, &nummiptex, sizeof (nummiptex));

    if (pattern)
    {
        fixpath (pattern, true);
    }

    for (i = 0; i < nummiptex; ++i)
    {
        mdl_seek (bsp, header.lumps[LUMP_TEXTURES].fileofs + sizeof (nummiptex) + sizeof (dataofs) * i, SEEK_SET);
        mdl_read (bsp, &dataofs, sizeof (dataofs));

        dataofs += header.lumps[LUMP_TEXTURES].fileofs;

        mdl_seek (bsp, dataofs, SEEK_SET);
        mdl_read (bsp, &mip, sizeof (mip));
        fixpath (mip.name, true);
        
        if (pattern && !strstr (mip.name, pattern))
        {
            continue;
        }

        for (j = 0; j < MIPLEVELS; ++j)
        {
            mip.offsets[j] += dataofs;
        }
        
        decomp_miptex (bsp, bmpdir, &mip);
    }

    fclose (bsp);

    fprintf (stdout, "Done!\n");
}
