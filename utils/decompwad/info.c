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

void info_mdl (const char *mdlname, const char *args)
{
    int id;
    int version;
    FILE *mdl = mdl_open (mdlname, &id, &version, false);

    fprintf (stdout, "Identifier: \"%.4s\"\n", (char *)&id);

    if (id == IDWADHEADER)
    {
        fprintf (stdout, "Valve WAD\n");

        wadinfo_t info;
        mdl_read (mdl, &info, sizeof (info));

        int i, j;

        lumpinfo_t lumpinfo;
        miptex_t mip;

        if (args)
        {
            fixpath (args, true);
        }

        int miptotal = 0;

        for (i = 0; i < info.numlumps; ++i)
        {
            mdl_seek (mdl, info.infotableofs + sizeof (lumpinfo) * i, SEEK_SET);
            mdl_read (mdl, &lumpinfo, sizeof (lumpinfo));

            switch (lumpinfo.type)
            {
            case TYP_MIPTEX:
                miptotal++;
                break;
            default:
                break;
            }
        }
        
        fprintf (stdout, "%i stored miptexs:\n", miptotal);

        int total = 0;

        for (i = 0; i < info.numlumps; ++i)
        {
            mdl_seek (mdl, info.infotableofs + sizeof (lumpinfo) * i, SEEK_SET);
            mdl_read (mdl, &lumpinfo, sizeof (lumpinfo));

            switch (lumpinfo.type)
            {
            case TYP_MIPTEX:
                mdl_seek (mdl, lumpinfo.filepos, SEEK_SET);
                mdl_read (mdl, &mip, sizeof (mip));
                fixpath (mip.name, true);

                if (args)
                {
                    if (!strstr (mip.name, args))
                    {
                        break;
                    }
                    total++;
                }

                fprintf (stdout, "    %.16s\n", mip.name);
                break;
            default:
                break;
            }
        }

        if (args)
        {
            fprintf (stdout, "%i results for \"%s\"\n", total, args);
        }

        goto info_done;
    }
    else if (id == BSPVERSION)
    {
        fprintf (stdout, "Valve BSP\n");

        dheader_t header;
        mdl_read (mdl, &header, sizeof (header));

        int i, j;

        int32_t nummiptex;
        int32_t dataofs;
        miptex_t mip;
        
        mdl_seek (mdl, header.lumps[LUMP_TEXTURES].fileofs, SEEK_SET);
        mdl_read (mdl, &nummiptex, sizeof (nummiptex));

        if (args)
        {
            fixpath (args, true);
        }
        
        fprintf (stdout, "%i stored miptexs:\n", nummiptex);

        int total = 0;

        for (i = 0; i < nummiptex; ++i)
        {
            mdl_seek (mdl, header.lumps[LUMP_TEXTURES].fileofs + sizeof (nummiptex) + sizeof (dataofs) * i, SEEK_SET);
            mdl_read (mdl, &dataofs, sizeof (dataofs));

            dataofs += header.lumps[LUMP_TEXTURES].fileofs;

            mdl_seek (mdl, dataofs, SEEK_SET);
            mdl_read (mdl, &mip, sizeof (mip));
            fixpath (mip.name, true);
            
            if (args)
            {
                if (!strstr (mip.name, args))
                {
                    continue;
                }
                total++;
            }

            fprintf (stdout, "    %.16s\n", mip.name);
        }

        if (args)
        {
            fprintf (stdout, "%i results for \"%s\"\n", total, args);
        }

        goto info_done;
    }
    else
    {
        fprintf (stdout, "Unknown\n");
        goto info_done;
    }
    
info_done:
    fclose (mdl);

    fprintf (stdout, "Done!\n");
}
