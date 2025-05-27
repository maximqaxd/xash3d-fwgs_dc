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
#include "sprite.h"
#include "wadlib.h"
#include "bspfile.h"

enum {
    kInfoAct   = 1 << 0,
    kInfoEvent = 1 << 1,
    kInfoBody  = 1 << 2,
};

void info_mdl (const char *mdlname, const char *args)
{
    int id;
    int version;
    FILE *mdl = mdl_open (mdlname, &id, &version, false);

    fprintf (stdout, "Identifier: \"%.4s\"\n", (char *)&id);

    if (id == IDSTUDIOHEADER)
    {
        fprintf (stdout, "Version: %i\n", version);

        if (version < STUDIO_VERSION)
        {
            fprintf (stdout, "Valve MDL (Alpha)\n");
            goto info_done;
        }
        
        int32_t textureindex;
        mdl_seek (mdl, offsetof (studiohdr_t, textureindex), SEEK_SET);
        mdl_read (mdl, &textureindex, sizeof (textureindex));

        if (textureindex == sizeof (studiohdr_t))
        {
            fprintf (stdout, "Valve MDL external texture group\n");
        }
        else
        {
            fprintf (stdout, "Valve MDL\n");
        }
    }
    else if (id == IDSTUDIOSEQHEADER)
    {
        fprintf (stdout, "Version: %i\n", version);
        
        fprintf (stdout, "Valve MDL sequence group\n");
    }
    else if (id == IDSPRITEHEADER)
    {
        fprintf (stdout, "Version: %i\n", version);
        
        if (version < SPRITE_VERSION)
        {
            fprintf (stdout, "Valve SPR (Alpha)\n");
            goto info_done;
        }

        fprintf (stdout, "Valve SPR\n");

        dsprite_t header;
        mdl_seek (mdl, 0, SEEK_SET);
        mdl_read (mdl, &header, sizeof (header));

        fprintf (stdout, "Sprite has %i frames, %i\u00d7%i pixels\n", header.numframes, header.width, header.height);
        
        goto info_done;
    }
    else if (id == IDWADHEADER)
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

    unsigned long mode = 0;
    
    if (args)
    {
        char *arg = strtok (args, ",");

        while (arg)
        {
            if (!strcasecmp (arg, "acts"))
            {
                mode |= kInfoAct;
            }
            else if (!strcasecmp (arg, "events"))
            {
                mode |= kInfoEvent;
            }
            else if (!strcasecmp (arg, "bodygroups"))
            {
                mode |= kInfoBody;
            }
            arg = strtok (NULL, ",");
        }
    }

    studiohdr_t header;
    mdl_seek (mdl, 0, SEEK_SET);
    mdl_read (mdl, &header, sizeof (header));

    fprintf (stdout, "Stored name: \"%.64s\"\n", header.name);
    
    fprintf (stdout, "%i sequences:\n", header.numseq);

    int i, j;
    mstudioseqdesc_t seq;
    mstudioevent_t event;
    
    for (i = 0; i < header.numseq; ++i)
    {
        mdl_seek (mdl, header.seqindex + sizeof (seq) * i, SEEK_SET);
        mdl_read (mdl, &seq, sizeof (seq));
        
        fprintf (stdout, "%4i : \"%s\"\n", i, seq.label);

        if (mode & kInfoAct && seq.activity > 0)
        {
            fprintf (stdout, "    %s\n", mdl_getactname (seq.activity));
        }

        if (mode & kInfoEvent && seq.numevents > 0)
        {
            fprintf (stdout, "    %i events:\n", seq.numevents);

            mdl_seek (mdl, seq.eventindex, SEEK_SET);
            
            for (j = 0; j < seq.numevents; ++j)
            {
                mdl_read (mdl, &event, sizeof (event));
                fprintf (stdout, "        %4i : %i\n", event.frame, event.event);
            }
        }
    }
    
    if (id != IDSTUDIOSEQHEADER && (mode & kInfoBody))
    {
        fprintf (stdout, "Body groups (%i):\n", header.numbodyparts);

        mstudiobodyparts_t bodypart;
        mstudiomodel_t model;

        for (i = 0; i < header.numbodyparts; ++i)
        {
            mdl_seek (mdl, header.bodypartindex + sizeof (bodypart) * i, SEEK_SET);
            mdl_read (mdl, &bodypart, sizeof (bodypart));
            fprintf (stdout, "%4i : \"%s\"\n", i, bodypart.name);

            mdl_seek (mdl, bodypart.modelindex, SEEK_SET);

            for (j = 0; j < bodypart.nummodels; ++j)
            {
                mdl_read (mdl, &model, sizeof (model));
                fprintf (stdout, "    %4i : \"%s\"\n", j, model.name);
            }
        }
    }

info_done:
    fclose (mdl);

    fprintf (stdout, "Done!\n");
}
