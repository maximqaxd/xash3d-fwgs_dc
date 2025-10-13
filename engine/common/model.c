/*
model.c - modelloader
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "common.h"
#include "mod_local.h"
#include "sprite.h"
#include "xash3d_mathlib.h"
#if !XASH_DREAMCAST
#include "alias.h"
#endif
#include "studio.h"
#include "wadfile.h"
#include "world.h"
#include "enginefeatures.h"
#include "client.h"
#include "server.h"

#if XASH_DREAMCAST
// Dynamic allocation on DC to save ~204KB of static data
// Start with 64 models (~50KB) and grow as needed
static model_info_t	*mod_crcinfo = NULL;
static model_t		*mod_known = NULL;
static int		mod_capacity = 0;
static poolhandle_t	mod_mempool = 0;
#else
static model_info_t	mod_crcinfo[MAX_MODELS];
static model_t		mod_known[MAX_MODELS];
#endif
static int	mod_numknown = 0;

#if XASH_DREAMCAST
// DC-only: LRU for studio model CPU blobs
static size_t g_dc_studio_bytes = 0;
CVAR_DEFINE( dc_studio_budget_kb, "dc_studio_budget_kb", "192", FCVAR_ARCHIVE, "Budget for studio CPU data (KB)" );
CVAR_DEFINE( dc_studio_keep_frames, "dc_studio_keep_frames", "120", FCVAR_ARCHIVE, "Frames to keep unused studio before evict" );
#endif
poolhandle_t      com_studiocache;		// cache for submodels
CVAR_DEFINE( mod_studiocache, "r_studiocache", "1", FCVAR_ARCHIVE, "enables studio cache for speedup tracing hitboxes" );
CVAR_DEFINE_AUTO( r_wadtextures, "0", 0, "completely ignore textures in the bsp-file if enabled" );
CVAR_DEFINE_AUTO( r_showhull, "0", 0, "draw collision hulls 1-3" );

/*
===============================================================================

			MOD COMMON UTILS

===============================================================================
*/
/*
================
Mod_Modellist_f
================
*/
static void Mod_Modellist_f( void )
{
	int	i, nummodels;
	model_t	*mod;

	Con_Printf( "\n" );
	Con_Printf( "-----------------------------------\n" );

	for( i = nummodels = 0, mod = mod_known; i < mod_numknown; i++, mod++ )
	{
		if( !COM_CheckStringEmpty( mod->name ) )
			continue; // free slot
		Con_Printf( "%s\n", mod->name );
		nummodels++;
	}

	Con_Printf( "-----------------------------------\n" );
	Con_Printf( "%i total models\n", nummodels );
	Con_Printf( "\n" );
}

#if XASH_DREAMCAST
void DC_Studio_EvictLRU( void )
{
    size_t budget = (size_t)(Q_atoi( dc_studio_budget_kb.string )) * 1024;
    int keep = Q_atoi( dc_studio_keep_frames.string );
    if( budget == 0 || keep <= 0 ) return;

    // compute total and find LRU candidate older than keep frames
    size_t total = 0; int lru = -1; size_t lru_size = 0; int i;
    for( i = 0; i < mod_numknown; i++ )
    {
        model_t *m = &mod_known[i];
        if( m->type != mod_studio || !m->cache.data ) continue;
        studiohdr_t *ph = (studiohdr_t *)m->cache.data;
        total += (size_t)ph->length;
    }
    if( total <= budget ) return;

    // Evict models not touched in last 'keep' frames, oldest first
    for( ;; )
    {
        lru = -1; lru_size = 0;
        for( i = 0; i < mod_numknown; i++ )
        {
            model_t *m = &mod_known[i];
            if( m->type != mod_studio || !m->cache.data ) continue;
            uint age = (uint)host.framecount - m->dc_last_used_frame;
            if( age <= (uint)keep ) continue;
            studiohdr_t *ph = (studiohdr_t *)m->cache.data;
            size_t sz = (size_t)ph->length;
            // Prefer evicting the largest eligible model to meet budget faster
            if( sz > lru_size ) { lru = i; lru_size = sz; }
        }
        if( lru == -1 ) break;
        model_t *m = &mod_known[lru];
#if !XASH_DEDICATED
        // Unload renderer-side textures while header is still available
        if( !Host_IsDedicated() )
            ref.dllFuncs.Mod_ProcessRenderData( m, false, NULL );
#endif
        studiohdr_t *ph = (studiohdr_t *)m->cache.data;
        size_t sz = (size_t)ph->length;
        Mem_Free( m->cache.data );
        m->cache.data = NULL;
        if( g_dc_studio_bytes >= sz ) g_dc_studio_bytes -= sz; else g_dc_studio_bytes = 0;
        Con_DPrintf( "dc_studio: evicted %s (%s)\n", m->name, Q_memprint( (int)sz ) );

        // recompute total and stop if under budget
        total = 0;
        for( i = 0; i < mod_numknown; i++ )
        {
            model_t *mm = &mod_known[i];
            if( mm->type != mod_studio || !mm->cache.data ) continue;
            studiohdr_t *ph2 = (studiohdr_t *)mm->cache.data;
            total += (size_t)ph2->length;
        }
        if( total <= budget ) break;
    }
}
#endif

/*
================
Mod_FreeUserData
================
*/
static void Mod_FreeUserData( model_t *mod )
{
	// ignore submodels and freed models
	if( !COM_CheckStringEmpty( mod->name ) || mod->name[0] == '*' )
		return;

	if( Host_IsDedicated() )
	{
		if( svgame.physFuncs.Mod_ProcessUserData != NULL )
		{
			// let the server.dll free custom data
			svgame.physFuncs.Mod_ProcessUserData( mod, false, NULL );
		}
	}
#if !XASH_DEDICATED
	else
	{
		ref.dllFuncs.Mod_ProcessRenderData( mod, false, NULL );
	}
#endif
}

/*
================
Mod_FreeModel
================
*/
void Mod_FreeModel( model_t *mod )
{
	// already freed?
	if( !mod || !COM_CheckStringEmpty( mod->name ) )
		return;

#if 0
        // Only the world model owns the allocations; submodels (*) share pointers
        if( mod->type != mod_brush || mod->name[0] != '*' )
        {
            extern uint8_t *pvr_pool; // Defined in zone.c
            void *alloc_base = alloc_base_address(pvr_pool);
            size_t alloc_size = alloc_block_count(pvr_pool) * 2048;

            if (mod->surfaces)
            {

                // Free a single contiguous extrasurf VRAM block if present
                mextrasurf_t *info0 = mod->surfaces[0].info;
                if( info0 &&
                    (uint8_t *)info0 >= (uint8_t *)alloc_base &&
                    (uint8_t *)info0 < (uint8_t *)alloc_base + alloc_size )
                {
                    alloc_free( pvr_pool, info0 );
                    for( int i = 0; i < mod->numsurfaces; i++ )
                        mod->surfaces[i].info = NULL;
                }

                // Free msurface array if it was allocated in VRAM
                if( (uint8_t *)mod->surfaces >= (uint8_t *)alloc_base &&
                    (uint8_t *)mod->surfaces < (uint8_t *)alloc_base + alloc_size )
                {
                    alloc_free( pvr_pool, mod->surfaces );
                    mod->surfaces = NULL;
                }
            }
        }
#endif

    if (mod->type != mod_brush || mod->name[0] != '*')
    {
        Mod_FreeUserData(mod);
        Mem_FreePool(&mod->mempool); // Frees main RAM allocations
    }

    if (mod->type == mod_brush && FBitSet(mod->flags, MODEL_WORLD))
    {
        world.version = 0;
        world.shadowdata = NULL;
        world.deluxedata = NULL;
        world.hull_models = NULL;
        world.compressed_phs = NULL;
        world.phsofs = NULL;
    }

    memset(mod, 0, sizeof(*mod));
}

/*
===============================================================================

			MODEL INITIALIZE\SHUTDOWN

===============================================================================
*/
/*
================
Mod_Init
================
*/
void Mod_Init( void )
{
	com_studiocache = Mem_AllocPool( "Studio Cache" );
	
#if XASH_DREAMCAST
	// Dynamically allocate model arrays to save ~204KB of static data
	// Start with 128 models (~100KB) and grow as needed
	mod_mempool = Mem_AllocPool( "Model Arrays" );
	mod_capacity = 512;
	
	mod_crcinfo = (model_info_t *)Mem_Calloc( mod_mempool, mod_capacity * sizeof(model_info_t) );
	mod_known = (model_t *)Mem_Calloc( mod_mempool, mod_capacity * sizeof(model_t) );
	
	Con_DPrintf( "Mod_Init: allocated %d model slots (%zu KB)\n", 
		mod_capacity, (mod_capacity * (sizeof(model_info_t) + sizeof(model_t))) / 1024 );
#endif

	Cvar_RegisterVariable( &mod_studiocache );
	Cvar_RegisterVariable( &r_wadtextures );
	Cvar_RegisterVariable( &r_showhull );
#if XASH_DREAMCAST
    Cvar_RegisterVariable( &dc_studio_budget_kb );
    Cvar_RegisterVariable( &dc_studio_keep_frames );
#endif
	Cmd_AddCommand( "mapstats", Mod_PrintWorldStats_f, "show stats for currently loaded map" );
	Cmd_AddCommand( "modellist", Mod_Modellist_f, "display loaded models list" );

	Mod_ResetStudioAPI ();
	Mod_InitStudioHull ();
}

/*
================
Mod_FreeAll
================
*/
void Mod_FreeAll( void )
{
	int	i;

#if !XASH_DEDICATED
	Mod_ReleaseHullPolygons();
#endif
	for( i = 0; i < mod_numknown; i++ )
		Mod_FreeModel( &mod_known[i] );
	mod_numknown = 0;
}

/*
================
Mod_ClearUserData
================
*/
void Mod_ClearUserData( void )
{
	int	i;

	for( i = 0; i < mod_numknown; i++ )
		Mod_FreeUserData( &mod_known[i] );
}

/*
================
Mod_Shutdown
================
*/
void Mod_Shutdown( void )
{
	Mod_FreeAll();
	Mem_FreePool( &com_studiocache );
	
#if XASH_DREAMCAST
	// Free dynamically allocated model arrays
	if( mod_mempool )
	{
		Mem_FreePool( &mod_mempool );
		mod_crcinfo = NULL;
		mod_known = NULL;
		mod_capacity = 0;
		mod_numknown = 0;
	}
#endif
}

/*
===============================================================================

			MODELS MANAGEMENT

===============================================================================
*/
/*
==================
Mod_FindName

never return NULL
==================
*/
model_t *Mod_FindName( const char *filename, qboolean trackCRC )
{
	char	modname[MAX_QPATH];
	model_t	*mod;
	int	i;

	Q_strncpy( modname, filename, sizeof( modname ));

	// search the currently loaded models
	for( i = 0, mod = mod_known; i < mod_numknown; i++, mod++ )
	{
		if( !Q_stricmp( mod->name, modname ))
		{
			if( mod->mempool || mod->name[0] == '*' )
				mod->needload = NL_PRESENT;
			else mod->needload = NL_NEEDS_LOADED;

			return mod;
		}
	}

	// find a free model slot spot
	for( i = 0, mod = mod_known; i < mod_numknown; i++, mod++ )
		if( !COM_CheckStringEmpty( mod->name ) ) break; // this is a valid spot

	if( i == mod_numknown )
	{
#if XASH_DREAMCAST
		// Check if we need to grow the arrays
		if( mod_numknown >= mod_capacity )
		{
			int new_capacity = mod_capacity * 2;
			model_info_t *new_crcinfo;
			model_t *new_known;
			
			if( new_capacity > MAX_MODELS )
				new_capacity = MAX_MODELS;
			
			if( mod_numknown >= MAX_MODELS )
			{
				Con_DPrintf( "MAX_MODELS limit exceeded (%d)\n", MAX_MODELS );
				mod_numknown++;
				return mod; // Return last slot (will likely crash, but matches old behavior)
			}
			
			Con_DPrintf( "Growing model arrays: %d -> %d (%zu KB)\n", 
				mod_capacity, new_capacity,
				(new_capacity * (sizeof(model_info_t) + sizeof(model_t))) / 1024 );
			
			// Allocate new arrays
			new_crcinfo = (model_info_t *)Mem_Calloc( mod_mempool, new_capacity * sizeof(model_info_t) );
			new_known = (model_t *)Mem_Calloc( mod_mempool, new_capacity * sizeof(model_t) );
			
			// Copy old data
			memcpy( new_crcinfo, mod_crcinfo, mod_capacity * sizeof(model_info_t) );
			memcpy( new_known, mod_known, mod_capacity * sizeof(model_t) );
			
			// Free old arrays
			Mem_Free( mod_crcinfo );
			Mem_Free( mod_known );
			
			// Update pointers
			mod_crcinfo = new_crcinfo;
			mod_known = new_known;
			mod = &mod_known[mod_numknown];
			mod_capacity = new_capacity;
		}
		mod_numknown++;
#else
		if( mod_numknown == MAX_MODELS )
			Host_Error( "MAX_MODELS limit exceeded (%d)\n", MAX_MODELS );
		mod_numknown++;
#endif
	}

	// copy name, so model loader can find model file
	Q_strncpy( mod->name, modname, sizeof( mod->name ));
	if( trackCRC ) mod_crcinfo[i].flags = FCRC_SHOULD_CHECKSUM;
	else mod_crcinfo[i].flags = 0;
	mod->needload = NL_NEEDS_LOADED;
	mod_crcinfo[i].initialCRC = 0;

	return mod;
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
model_t *Mod_LoadModel( model_t *mod, qboolean crash )
{
	char		tempname[MAX_QPATH];
	fs_offset_t		length = 0;
	qboolean		loaded;
	byte		*buf;
	model_info_t	*p;

	ASSERT( mod != NULL );

	// check if already loaded (or inline bmodel)
	if( mod->mempool || mod->name[0] == '*' )
	{
		mod->needload = NL_PRESENT;
		return mod;
	}

	ASSERT( mod->needload == NL_NEEDS_LOADED );

	// store modelname to show error
	Q_strncpy( tempname, mod->name, sizeof( tempname ));
	COM_FixSlashes( tempname );

	buf = FS_LoadFile( tempname, &length, false );

	if( !buf )
	{
		memset( mod, 0, sizeof( model_t ));

		if( crash ) Host_Error( "Could not load model %s from disk\n", tempname );
		else Con_Printf( S_ERROR "Could not load model %s from disk\n", tempname );

		return NULL;
	}

	Con_Reportf( "loading %s\n", mod->name );
	mod->needload = NL_PRESENT;
	mod->type = mod_bad;

	// call the apropriate loader
	switch( *(uint *)buf )
	{
	case IDSTUDIOHEADER:
		Mod_LoadStudioModel( mod, buf, &loaded );
		break;
	case IDSPRITEHEADER:
#if XASH_DREAMCAST
		_Mod_LoadSpriteModel( mod, buf, &loaded );
#else
		Mod_LoadSpriteModel( mod, buf, &loaded );
#endif
		break;
#if !XASH_DREAMCAST
	case IDALIASHEADER:
		Mod_LoadAliasModel( mod, buf, &loaded );
		return NULL;
		break;
#endif
	case Q1BSP_VERSION:
	case HLBSP_VERSION:
	case QBSP2_VERSION:
		Mod_LoadBrushModel( mod, buf, &loaded );
		break;
	default:
		Mem_Free( buf );
		if( crash ) Host_Error( "%s has unknown format\n", tempname );
		else Con_Printf( S_ERROR "%s has unknown format\n", tempname );
		return NULL;
	}

	if( loaded )
	{
		if( world.loading )
			SetBits( mod->flags, MODEL_WORLD ); // mark worldmodel

		if( Host_IsDedicated() )
		{
			if( svgame.physFuncs.Mod_ProcessUserData != NULL )
			{
				// let the server.dll load custom data
				svgame.physFuncs.Mod_ProcessUserData( mod, true, buf );
			}
		}
#if !XASH_DEDICATED
		else
		{
			loaded = ref.dllFuncs.Mod_ProcessRenderData( mod, true, buf );
		}
#endif
	}
#if !XASH_DREAMCAST
	if( mod->type == mod_alias )
	{
		aliashdr_t *hdr = mod->cache.data;
		if( hdr ) // clean up temporary pointer after passing the alias model to the renderer
			hdr->pposeverts = NULL;
	}
#endif
	if( !loaded )
	{
		Mod_FreeModel( mod );
		Mem_Free( buf );

		if( crash ) Host_Error( "Could not load model %s\n", tempname );
		else Con_Printf( S_ERROR "Could not load model %s\n", tempname );

		return NULL;
	}

	p = &mod_crcinfo[mod - mod_known];
	mod->needload = NL_PRESENT;

	if( FBitSet( p->flags, FCRC_SHOULD_CHECKSUM ))
	{
		uint32_t currentCRC;

		CRC32_Init( &currentCRC );
		CRC32_ProcessBuffer( &currentCRC, buf, length );
		currentCRC = CRC32_Final( currentCRC );

		if( FBitSet( p->flags, FCRC_CHECKSUM_DONE ))
		{
			if( currentCRC != p->initialCRC )
				Host_Error( "%s has a bad checksum\n", tempname );
		}
		else
		{
			SetBits( p->flags, FCRC_CHECKSUM_DONE );
			p->initialCRC = currentCRC;
		}
	}
	Mem_Free( buf );

	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName( const char *name, qboolean crash, qboolean trackCRC )
{
	model_t	*mod;

	if( !COM_CheckString( name ))
		return NULL;

	mod = Mod_FindName( name, trackCRC );
	return Mod_LoadModel( mod, crash );
}

/*
==================
Mod_PurgeStudioCache

free studio cache on change level
==================
*/
static void Mod_PurgeStudioCache( void )
{
	int	i;

	// refresh hull data
	SetBits( r_showhull.flags, FCVAR_CHANGED );
#if !XASH_DEDICATED
	Mod_ReleaseHullPolygons();
#endif
	// release previois map
	Mod_FreeModel( mod_known );	// world is stuck on slot #0 always

    // we should release all the world submodels
    // and clear studio sequences
    for( i = 1; i < mod_numknown; i++ )
    {
        model_t *m = &mod_known[i];
        if( m->type == mod_studio )
            m->submodels = NULL;
        if( m->name[0] == '*' )
            Mod_FreeModel( m );
        m->needload = NL_UNREFERENCED;
    }

	Mem_EmptyPool( com_studiocache );
	Mod_ClearStudioCache();

#if XASH_DREAMCAST
    // DC: bulk-evict all studio CPU blobs and GL textures on changelevel
    for( i = 1; i < mod_numknown; i++ )
    {
        model_t *m = &mod_known[i];
        if( m->type != mod_studio || !m->cache.data )
            continue;
#if !XASH_DEDICATED
        if( !Host_IsDedicated() )
            ref.dllFuncs.Mod_ProcessRenderData( m, false, NULL );
#endif
        studiohdr_t *ph = (studiohdr_t *)m->cache.data;
        size_t sz = (size_t)ph->length;
        Mem_Free( m->cache.data );
        m->cache.data = NULL;
        if( g_dc_studio_bytes >= sz ) g_dc_studio_bytes -= sz; else g_dc_studio_bytes = 0;
        m->dc_last_used_frame = 0;
        Con_DPrintf( "dc_studio: evicted (changelevel) %s (%s)\n", m->name, Q_memprint( (int)sz ) );
    }
#endif
}

/*
==================
Mod_LoadWorld

Loads in the map and all submodels
==================
*/
model_t *Mod_LoadWorld( const char *name, qboolean preload )
{
	model_t	*pworld;

	// already loaded?
	if( !Q_stricmp( mod_known->name, name ))
		return mod_known;

	// free sequence files on studiomodels
	Mod_PurgeStudioCache();

	// load the newmap
	world.loading = true;
	pworld = Mod_FindName( name, false );
	if( preload ) Mod_LoadModel( pworld, true );
	world.loading = false;

	ASSERT( pworld == mod_known );

	return pworld;
}

/*
==================
Mod_FreeUnused

Purge all unused models
==================
*/
void Mod_FreeUnused( void )
{
	model_t	*mod;
	int	i;

	// never tries to release worldmodel
	for( i = 1, mod = &mod_known[1]; i < mod_numknown; i++, mod++ )
	{
		if( mod->needload == NL_UNREFERENCED && COM_CheckString( mod->name ))
			Mod_FreeModel( mod );
	}
}

/*
===============================================================================

			MODEL ROUTINES

===============================================================================
*/
/*
===============
Mod_Calloc

===============
*/
void *Mod_Calloc( int number, size_t size )
{
	cache_user_t	*cu;

	if( number <= 0 || size <= 0 ) return NULL;
	cu = (cache_user_t *)Mem_Calloc( com_studiocache, sizeof( cache_user_t ) + number * size );
	cu->data = (void *)cu; // make sure what cu->data is not NULL

	return cu;
}

/*
===============
Mod_CacheCheck

===============
*/
void *Mod_CacheCheck( cache_user_t *c )
{
	return Cache_Check( com_studiocache, c );
}

/*
===============
Mod_LoadCacheFile

===============
*/
void Mod_LoadCacheFile( const char *filename, cache_user_t *cu )
{
	char	modname[MAX_QPATH];
	fs_offset_t	size;
	byte	*buf;

	Assert( cu != NULL );

	if( !COM_CheckString( filename ))
		return;

	Q_strncpy( modname, filename, sizeof( modname ));
	COM_FixSlashes( modname );

	buf = FS_LoadFile( modname, &size, false );
	if( !buf || !size ) Host_Error( "LoadCacheFile: ^1can't load %s^7\n", filename );
	cu->data = Mem_Malloc( com_studiocache, size );
	memcpy( cu->data, buf, size );
	Mem_Free( buf );
}

/*
===============
Mod_AliasExtradata

===============
*/
#if !XASH_DREAMCAST
void *Mod_AliasExtradata( model_t *mod )
{
	if( mod && mod->type == mod_alias )
		return mod->cache.data;
	return NULL;
}
#endif
/*
===============
Mod_StudioExtradata

===============
*/
void *Mod_StudioExtradata( model_t *mod )
{
    if( mod && mod->type == mod_studio )
    {
#if XASH_DREAMCAST
        // Touch last used on DC
        mod->dc_last_used_frame = host.framecount;
        // If evicted, reload a minimal CPU blob (without textures)
        if( !mod->cache.data )
        {
            char modname[MAX_QPATH];
            fs_offset_t size = 0;
            byte *buf;

            Q_strncpy( modname, mod->name, sizeof( modname ) );
            COM_FixSlashes( modname );

            buf = FS_LoadFile( modname, &size, false );
            if( buf && size )
            {
                studiohdr_t *hdr_in = (studiohdr_t *)buf;
                size_t full_size = (size_t)hdr_in->length;

                // load full header first so renderer can rebuild texture bindings
                void *newdata = Mem_Calloc( mod->mempool, full_size );
                memcpy( newdata, buf, full_size );
                mod->cache.data = newdata;

                studiohdr_t *ph = (studiohdr_t *)mod->cache.data;
#if !XASH_DEDICATED
                if( !Host_IsDedicated() )
                {
                    if( ph->numtextures > 0 )
                    {
                        // Regular case: textures embedded
                        ref.dllFuncs.Mod_StudioLoadTextures( mod, ph );
                    }
                    else
                    {
                        // No embedded textures: try load and merge T.mdl like initial loader does
                        studiohdr_t *thdr;
                        void *buffer2;

                        buffer2 = FS_LoadFile( Mod_StudioTexName( mod->name ), NULL, false );
                        thdr = (studiohdr_t *)buffer2;
                        if( thdr != NULL && thdr->length >= (int)sizeof( studiohdr_t ) && thdr->version == STUDIO_VERSION )
                        {
                            byte *in, *out;
                            size_t size1, size2;

                            // build GL textures using texture header
                            ref.dllFuncs.Mod_StudioLoadTextures( mod, thdr );

                            // merge texture and skinref arrays into main header so renderer can index them
                            size1 = thdr->numtextures * sizeof( mstudiotexture_t );
                            size2 = thdr->numskinfamilies * thdr->numskinref * sizeof( short );

                            void *merged = Mem_Calloc( mod->mempool, ph->length + size1 + size2 );
                            memcpy( merged, ph, ph->length );
                            Mem_Free( ph );
                            mod->cache.data = merged;
                            ph = (studiohdr_t *)mod->cache.data;
                            ph->numskinfamilies = thdr->numskinfamilies;
                            ph->numtextures = thdr->numtextures;
                            ph->numskinref = thdr->numskinref;
                            ph->textureindex = ph->length;
                            ph->skinindex = ph->textureindex + size1;

                            in = (byte *)thdr + thdr->textureindex;
                            out = (byte *)ph + ph->textureindex;
                            memcpy( out, in, size1 + size2 );
                            ph->length += size1 + size2;
                        }
                        else Con_Printf( S_WARN "%s: %s missing or invalid textures file (reload)\n", __func__, mod->name );

                        if( buffer2 )
                            Mem_Free( buffer2 );
                    }
                }
#endif

                // Optionally drop CPU-side texture pixels to save RAM
                if( ph->texturedataindex > 0 && ph->texturedataindex < ph->length )
                {
                    size_t trimmed = (size_t)ph->texturedataindex;
                    void *trimmed_ptr = Mem_Realloc( mod->mempool, mod->cache.data, trimmed );
                    if( trimmed_ptr )
                    {
                        mod->cache.data = trimmed_ptr;
                        ph = (studiohdr_t *)mod->cache.data;
                        ph->length = (int)trimmed;
                        g_dc_studio_bytes += trimmed;
                        Con_DPrintf( "dc_studio: reloaded %s (%s, trimmed)\n", mod->name, Q_memprint( (int)trimmed ) );
                    }
                    else
                    {
                        // fallback keep full if realloc failed
                        g_dc_studio_bytes += full_size;
                        Con_DPrintf( "dc_studio: reloaded %s (%s)\n", mod->name, Q_memprint( (int)full_size ) );
                    }
                }
                else
                {
                    g_dc_studio_bytes += full_size;
                    Con_DPrintf( "dc_studio: reloaded %s (%s)\n", mod->name, Q_memprint( (int)full_size ) );
                }
            }
            else
            {
                Con_Printf( S_ERROR "dc_studio: failed to reload %s\n", mod->name );
            }
            if( buf ) Mem_Free( buf );
        }
#endif
        return mod->cache.data;
    }
	return NULL;
}

/*
==================
Mod_ValidateCRC

==================
*/
qboolean Mod_ValidateCRC( const char *name, CRC32_t crc )
{
	model_info_t	*p;
	model_t		*mod;

	mod = Mod_FindName( name, true );
	p = &mod_crcinfo[mod - mod_known];

	if( !FBitSet( p->flags, FCRC_CHECKSUM_DONE ))
		return true;
	if( p->initialCRC == crc )
		return true;
	return false;
}

/*
==================
Mod_NeedCRC

==================
*/
void Mod_NeedCRC( const char *name, qboolean needCRC )
{
	model_t		*mod;
	model_info_t	*p;

	mod = Mod_FindName( name, true );
	p = &mod_crcinfo[mod - mod_known];

	if( needCRC ) SetBits( p->flags, FCRC_SHOULD_CHECKSUM );
	else ClearBits( p->flags, FCRC_SHOULD_CHECKSUM );
}
