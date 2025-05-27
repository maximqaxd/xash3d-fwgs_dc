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

void decomp_mdl (
    const char *mdlname,
    const char *qcname,
    const char *cd,
    const char *cdtexture,
    const char *cdanim,
    const char *qcdir,
    const char *smddir);

void decomp_spr (
    const char *sprname,
    const char *qcname,
    const char *cd,
    const char *qcdir,
    const char *bmpdir);

void decomp_wad (
    const char *wadname,
    const char *bmpdir,
    const char *pattern);

void decomp_bsptex (
    const char *bspname,
    const char *bmpdir,
    const char *pattern);

void info_mdl (
    const char *mdlname,
    const char *args);

static int getargs (
    int argc,
    char **argv,
    char **cd,
    bool *havecd,
    char **cdtexture,
    char **cdanim,
    char **wadpattern)
{
    if (argc < 2)
    {
print_help:
        fprintf (stdout, "Usage: decompmdl [options...] <input {*.mdl | *.spr | *.wad | *.bsp}> [<output {directory | *.qc}>]\n\n");
        fprintf (stdout, "Options:\n");
        fprintf (stdout, "\t-help\t\t\tDisplay this message and exit.\n\n");

        fprintf (stdout,
"\t-cd <path>\t\tSets the data path. Defaults to \".\".\n\
\t\t\t\tIf set, data will be placed relative to root path.\n\n");
        
        fprintf (stdout,
"\t-cdtexture <path>\tSets the texture path, relative to data path.\n\
\t\t\t\tDefaults to \"./maps_8bit\" for models, and \"./bmp\"\n\
\t\t\t\tfor sprites, WADs, and BSPs.\n\n");
        
        fprintf (stdout,
"\t-cdanim <path>\t\tSets the animation path, relative to data path.\n\
\t\t\t\tDefaults to \"./anims\".\n\n");
        
        fprintf (stdout,
"\t-pattern <string>\tIf set, only textures containing the matching\n\
\t\t\t\tsubstring will be extracted from WADs and BSPs.\n\n");
        
        fprintf (stdout,
"\t-info [<string>]\tFile info will be printed. No decompiling will occur.\n\
\n\
\t\t\t\tA comma separated list of following arguments may be\n\
\t\t\t\tadded to print extra info: \"acts\" \"events\" \"bodygroups\"\n\
\n\
\t\t\t\tIf the input file is a WAD or BSP, the optional string\n\
\t\t\t\twill instead act identically to the \"-pattern\" option.\n\n");
        exit (0);
    }

    int i;

    for (i = 1; i < argc; ++i)
    {
        if (argv[i][0] != '-')
            break;
        
        if (!strcmp (argv[i], "-help"))
        {
            goto print_help;
        }
        else if (!strcmp (argv[i], "-info"))
        {
            if (i < argc - 1)
            {
                info_mdl (argv[argc - 1], (i + 1 < argc - 1) ? argv[i + 1] : NULL);
                exit (0);
            }
            goto print_help;
        }
        else if (!strcmp (argv[i], "-cd"))
        {
            *cd = argv[i + 1];
            *havecd = true;
            fprintf (stdout, "Data path set to: \"%s\"\n", *cd);
            ++i;
        }
        else if (!strcmp (argv[i], "-cdtexture"))
        {
            *cdtexture = argv[i + 1];
            fprintf (stdout, "Texture path set to: \"%s\"\n", *cdtexture);
            ++i;
        }
        else if (!strcmp (argv[i], "-cdanim"))
        {
            *cdanim = argv[i + 1];
            fprintf (stdout, "Animation path set to: \"%s\"\n", *cdanim);
            ++i;
        }
        else if (!strcmp (argv[i], "-pattern"))
        {
            *wadpattern = argv[i + 1];
            fprintf (stdout, "WAD search pattern set to: \"%s\"\n", *wadpattern);
            ++i;
        }
        else
        {
            fprintf (stdout, "Unknown option: \"%s\"\n", argv[i]);
        }
    }

    if (i >= argc)
    {
        error (1, "No input file provided\n");
    }

    return i;
}

static void getdirs (
    char *in,
    char *out,
    char **qcdir,
    char **qcname,
    bool havecd)
{
    if (havecd)
    {
        *qcdir = ".";
    }

    if (!out) /* Put files in sub directory. */
    {
        *qcname = strdup (skippath (in));
        stripext (*qcname);

        if (!havecd)
        {
            *qcdir = strdup (*qcname);
        }
        return;
    }

    char *name, *ext;
    
    filebase (out, &name, &ext);
    
    if (*ext) /* QC name provided. Put files in root directory. */
    {
        *qcname = strdup (out);
        stripext (*qcname);

        if (!havecd)
        {
            *qcdir = strdup (out);
            stripfilename (*qcdir);
        }
    }
    else /* Put files in sub directory. */
    {
        *qcname = strdup (skippath (in));
        stripext (*qcname);

        if (!havecd)
        {
            *qcdir = appenddir (out, *qcname);
        }
    }
}

int main (int argc, char **argv)
{
    char *cd = ".";
    bool havecd = false;
    char *cdtexture = NULL;
    char *cdanim = NULL;
    char *wadpattern = NULL;

    int i = getargs (argc, argv, &cd, &havecd, &cdtexture, &cdanim, &wadpattern);
    
    char *in = argv[i];
    char *out = (i + 1 < argc) ? argv[i + 1] : NULL;

    char *qcdir, *qcname;

    getdirs (in, out, &qcdir, &qcname, havecd);

    char *smddir = appenddir (qcdir, cd);

    char *name, *ext;
    filebase (in, &name, &ext);
    if (!strcasecmp (ext, ".spr"))
    {
        if (cdtexture == NULL)
            cdtexture = "./bmp";

        char *bmpdir = appenddir (qcdir, cdtexture);
        decomp_spr (in, skippath (qcname), cdtexture, qcdir, bmpdir);
        free (bmpdir);
    }
    else if (!strcasecmp (ext, ".wad"))
    {
        if (cdtexture == NULL)
            cdtexture = "./bmp";

        char *bmpdir = appenddir (qcdir, cdtexture);
        decomp_wad (in, bmpdir, wadpattern);
        free (bmpdir);
    }
    else if (!strcasecmp (ext, ".bsp"))
    {
        if (cdtexture == NULL)
            cdtexture = "./bmp";

        char *bmpdir = appenddir (qcdir, cdtexture);
        decomp_bsptex (in, bmpdir, wadpattern);
        free (bmpdir);
    }
    else
    {
        if (cdtexture == NULL)
            cdtexture = "./maps_8bit";
        
        if (cdanim == NULL)
            cdanim = "./anims";
        
        decomp_mdl (in, skippath (qcname), cd, cdtexture, cdanim, qcdir, smddir);
    }

    free (smddir);
    free (qcname);
    
    if (!havecd)
    {
        free (qcdir);
    }

    return 0;
}