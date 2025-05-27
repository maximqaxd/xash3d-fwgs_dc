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

#include "studio.h"
#include "bitmap.h"

void decomp_writebmp (FILE *bmp, byte *data, int width, int height, byte *palette)
{
    int real_width = ((width + 3) & ~3);
    int area = real_width * height;

    BITMAPFILEHEADER header;

    header.bfType = ('M' << 8) + 'B';

    header.bfOffBits = sizeof (BITMAPFILEHEADER)
                        + sizeof (BITMAPINFOHEADER)
                        + 256 * sizeof (RGBQUAD);
    
    header.bfSize = header.bfOffBits + area;
    
    header.bfReserved1 = 0;
    header.bfReserved2 = 0;

    qc_writeb (bmp, &header, sizeof (header));

    BITMAPINFOHEADER info;

    info.biSize = sizeof (BITMAPINFOHEADER);
    info.biWidth = real_width;
    info.biHeight = height;
    info.biPlanes = 1;
    info.biBitCount = 8;
    info.biCompression = BI_RGB;
    info.biSizeImage = 0;

    info.biXPelsPerMeter = 0;
    info.biYPelsPerMeter = 0;

    info.biClrUsed = 256;
    info.biClrImportant = 0;

    qc_writeb (bmp, &info, sizeof (info));

    int i;
    RGBQUAD rgba_palette[256];

    for (i = 0; i < 256; ++i)
    {
        rgba_palette[i].rgbRed = *palette++;
        rgba_palette[i].rgbGreen = *palette++;
        rgba_palette[i].rgbBlue = *palette++;
        rgba_palette[i].rgbReserved = 0;
    }

    qc_writeb (bmp, rgba_palette, 256 * sizeof (RGBQUAD));

    /* Reverse the order of the data. */

    byte *bmp_data = (byte *)memalloc (area, 1);
    data += (height - 1) * width;

    for (i = 0; i < height; ++i)
    {
        memmove (&bmp_data[real_width * i], data, width);
        data -= width;
    }

    qc_writeb (bmp, bmp_data, area);
    free (bmp_data);
}

void decomp_studiotexture (FILE *tex, const char *bmpdir, mstudiotexture_t *texture)
{
    int area = texture->width * texture->height;

    byte *data = (byte *)memalloc (area + 768, 1);
    byte *palette = data + area;
    
    mdl_seek (tex, texture->index, SEEK_SET);
    mdl_read (tex, data, area + 768);

    FILE *bmp = qc_open (bmpdir, skippath (texture->name), "bmp", true);

    decomp_writebmp (bmp, data, texture->width, texture->height, palette);

    free (data);
    fclose (bmp);
}
