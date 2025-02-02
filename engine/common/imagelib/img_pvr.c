/*
img_pvr.c - pvr format load
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

#include "imagelib.h"
#include "xash3d_mathlib.h"
#include "img_pvr.h"

/*
=============
Image_LoadPVR
=============
*/
qboolean Image_LoadPVR(const char *name, const byte *buffer, fs_offset_t filesize) 
{
    byte *texture_data;
    pvrt_t *pvrt;
    
    if (filesize < sizeof(pvrt_t)) {
        Con_DPrintf(S_ERROR "%s: (%s) file too small\n", __func__, name);
        return false;
    }

    // Check if we have GBIX header
    if (*(uint32_t*)buffer == GBIXHEADER) 
    {
        if (filesize < sizeof(gbix_t) + sizeof(pvrt_t)) {
            Con_DPrintf(S_ERROR "%s: (%s) file too small for GBIX format\n", __func__, name);
            return false;
        }

        gbix_t *gbix = (gbix_t *)buffer;
        texture_data = (byte *)buffer + sizeof(gbix_t) + gbix->nextTagOffset;
        pvrt = (pvrt_t *)texture_data;
        
        Con_DPrintf("GBIX nextTagOffset: %d\n", gbix->nextTagOffset);
    }
    else if (*(uint32_t*)buffer == PVRTSIGN)  // Standalone PVRT
    {
        pvrt = (pvrt_t *)buffer;
        texture_data = (byte *)buffer + sizeof(pvrt_t);
    }
    else 
    {
        Con_DPrintf(S_ERROR "%s: (%s) invalid format - no GBIX or PVRT header found\n", __func__, name);
        return false;
    }

    image.width = pvrt->width;
    image.height = pvrt->height;


    switch (pvrt->imageFormat)
    {
        case PVR_VQ:
            if (pvrt->colorFormat == PVR_ARGB4444)
                image.type= PF_VQ_ARGB_4444;
            else        
                image.type = PF_VQ_RGB_5650;
            image.size = 2048 + ((image.width * image.height) / 4);
            break;
        case PVR_RECT:
            image.type = PF_RGB_5650;
            image.size = image.width * image.height * 2;
            break;

        case PVR_TWIDDLE:
            Con_DPrintf("%s: Twiddle format is not supported yet\n", __func__);
            return false;

        default:
            Con_DPrintf(S_ERROR "%s: (%s) unsupported PVR format %02x\n", __func__, name, pvrt->imageFormat);
            return false;
    }

    image.rgba = Mem_Malloc(host.imagepool, image.size);
    memcpy(image.rgba, texture_data, image.size);

    return true;
}
