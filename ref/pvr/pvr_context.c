/*
vid_sdl.c - SDL vid component
Copyright (C) 2018 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "pvr_local.h"


ref_api_t      gEngfuncs;
ref_globals_t *gpGlobals;
ref_client_t  *gp_cl;
ref_host_t    *gp_host;

void GL_Mem_Free( void *data, const char *filename, int fileline )
{
	gEngfuncs._Mem_Free( data, filename, fileline );
}

void *GL_Mem_Alloc( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline )
{
	return gEngfuncs._Mem_Alloc( poolptr, size, clear, filename, fileline );
}

static void R_ClearScreen( void )
{
#if 0
	pglClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	pglClear( GL_COLOR_BUFFER_BIT );
#endif // TODO
}

static const byte *R_GetTextureOriginalBuffer( unsigned int idx )
{
	gl_texture_t *glt = R_GetTexture( idx );

	if( !glt || !glt->original || !glt->original->buffer )
		return NULL;

	return glt->original->buffer;
}

/*
=============
CL_FillRGBA

=============
*/
static void CL_FillRGBA( int rendermode, float _x, float _y, float _w, float _h, byte r, byte g, byte b, byte a )
{
	// Skip if fully transparent
	if( a == 0 )
		return;

	// Ensure TR list is open for 2D drawing
	if( g_pvr_current_list != PVR_LIST_TR_POLY )
	{
		// If we're not in 2D mode, this might be called outside normal rendering
		// Just skip silently to avoid corruption
		return;
	}

	// Convert screen coordinates to clip space (-1 to 1)
	// PVR uses screen space directly when 2D mode is active
	const float screen_width = (float)gpGlobals->width;
	const float screen_height = (float)gpGlobals->height;
	
	// Convert to normalized device coordinates
	float x1 = (_x / screen_width) * 2.0f - 1.0f;
	float y1 = 1.0f - ((_y + _h) / screen_height) * 2.0f; // Y is flipped
	float x2 = ((_x + _w) / screen_width) * 2.0f - 1.0f;
	float y2 = 1.0f - (_y / screen_height) * 2.0f;

	// Pack color (ARGB format)
	uint32_t argb = (a << 24) | (r << 16) | (g << 8) | b;

	// Initialize DR state
	pvr_dr_state_t dr_state;
	pvr_dr_init( &dr_state );

	// Setup colored polygon context (no texture)
	pvr_poly_cxt_t cxt;
	pvr_poly_cxt_col( &cxt, PVR_LIST_TR_POLY );
	cxt.gen.culling = PVR_CULLING_NONE;
	cxt.gen.fog_type = PVR_FOG_DISABLE;
	cxt.gen.alpha = PVR_ALPHA_ENABLE;
	cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS; // Always pass depth test for 2D
	cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
	
	// Set blend mode based on rendermode
	if( rendermode == kRenderTransAdd )
	{
		// Additive blending
		cxt.blend.src = PVR_BLEND_SRCALPHA;
		cxt.blend.dst = PVR_BLEND_ONE;
	}
	else
	{
		// Normal alpha blending
		cxt.blend.src = PVR_BLEND_SRCALPHA;
		cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
	}

	// Submit header
	pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target( dr_state );
	pvr_poly_compile( hdr, &cxt );
	pvr_dr_commit( hdr );

	// Draw quad as two triangles
	// GL order: (_x, _y), (_x+_w, _y), (_x+_w, _y+_h), (_x, _y+_h)
	// In screen space: top-left, top-right, bottom-right, bottom-left
	// In clip space: (x1, y2), (x2, y2), (x2, y1), (x1, y1)
	// Triangle 1: top-left, top-right, bottom-left
	pvr_vertex_t *vert = pvr_dr_target( dr_state );
	vert->flags = PVR_CMD_VERTEX;
	vert->x = x1; // top-left
	vert->y = y2;
	vert->z = 0.1f; // Small Z for 2D (in front of everything)
	vert->u = 0.0f;
	vert->v = 0.0f;
	vert->argb = argb;
	vert->oargb = 0;
	pvr_dr_commit( vert );

	vert = pvr_dr_target( dr_state );
	vert->flags = PVR_CMD_VERTEX;
	vert->x = x2; // top-right
	vert->y = y2;
	vert->z = 0.1f;
	vert->u = 0.0f;
	vert->v = 0.0f;
	vert->argb = argb;
	vert->oargb = 0;
	pvr_dr_commit( vert );

	vert = pvr_dr_target( dr_state );
	vert->flags = PVR_CMD_VERTEX_EOL;
	vert->x = x1; // bottom-left
	vert->y = y1;
	vert->z = 0.1f;
	vert->u = 0.0f;
	vert->v = 0.0f;
	vert->argb = argb;
	vert->oargb = 0;
	pvr_dr_commit( vert );

	// Triangle 2: top-right, bottom-right, bottom-left
	vert = pvr_dr_target( dr_state );
	vert->flags = PVR_CMD_VERTEX;
	vert->x = x2; // top-right
	vert->y = y2;
	vert->z = 0.1f;
	vert->u = 0.0f;
	vert->v = 0.0f;
	vert->argb = argb;
	vert->oargb = 0;
	pvr_dr_commit( vert );

	vert = pvr_dr_target( dr_state );
	vert->flags = PVR_CMD_VERTEX;
	vert->x = x2; // bottom-right
	vert->y = y1;
	vert->z = 0.1f;
	vert->u = 0.0f;
	vert->v = 0.0f;
	vert->argb = argb;
	vert->oargb = 0;
	pvr_dr_commit( vert );

	vert = pvr_dr_target( dr_state );
	vert->flags = PVR_CMD_VERTEX_EOL;
	vert->x = x1; // bottom-left
	vert->y = y1;
	vert->z = 0.1f;
	vert->u = 0.0f;
	vert->v = 0.0f;
	vert->argb = argb;
	vert->oargb = 0;
	pvr_dr_commit( vert );

	pvr_dr_finish();
}

static qboolean Mod_LooksLikeWaterTexture( const char *name )
{
	if(( name[0] == '*' && Q_stricmp( name, REF_DEFAULT_TEXTURE )) || name[0] == '!' )
		return true;

	if( !ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ))
	{
		if( !Q_strncmp( name, "water", 5 ) || !Q_strnicmp( name, "laser", 5 ))
			return true;
	}

	return false;
}
static void Mod_BrushUnloadTextures( model_t *mod )
{
	int i;

	for( i = 0; i < mod->numtextures; i++ )
	{
		texture_t *tx = mod->textures[i];
		if( !tx )
			continue; // free slot

		if( tx->gl_texturenum != tr.defaultTexture )
			GL_FreeTexture( tx->gl_texturenum ); // main texture
		if( !Mod_LooksLikeWaterTexture( tx->name ))
		{
			GL_FreeTexture( tx->fb_texturenum ); // luma texture
			GL_FreeTexture( tx->dt_texturenum ); // detail texture
		}
	}
}

static void Mod_UnloadTextures( model_t *mod )
{
	Assert( mod != NULL );

	switch( mod->type )
	{
	case mod_studio:
		Mod_StudioUnloadTextures( mod->cache.data );
		break;
	case mod_brush:
		Mod_BrushUnloadTextures( mod );
		break;
	case mod_sprite:
		Mod_SpriteUnloadTextures( mod->cache.data );
		break;
	default:
		ASSERT( 0 );
		break;
	}
}

static qboolean Mod_ProcessRenderData( model_t *mod, qboolean create, const byte *buf )
{
	qboolean loaded = false;

	if( !create )
	{
		if( gEngfuncs.drawFuncs->Mod_ProcessUserData )
			gEngfuncs.drawFuncs->Mod_ProcessUserData( mod, false, buf );
		Mod_UnloadTextures( mod );
		return true;
	}

	switch( mod->type )
	{
	case mod_studio:
	case mod_brush:
		loaded = true;
		break;
	case mod_sprite:
		Mod_LoadSpriteModel( mod, buf, &loaded, mod->numtexinfo );
		break;
	default:
		gEngfuncs.Host_Error( "%s: unsupported type %d\n", __func__, mod->type );
		return false;
	}

	if( gEngfuncs.drawFuncs->Mod_ProcessUserData )
		gEngfuncs.drawFuncs->Mod_ProcessUserData( mod, true, buf );

	return loaded;
}

static int GL_RefGetParm( int parm, int arg )
{
	gl_texture_t *glt;

	switch( parm )
	{
	case PARM_TEX_WIDTH:
		glt = R_GetTexture( arg );
		return glt->width;
	case PARM_TEX_HEIGHT:
		glt = R_GetTexture( arg );
		return glt->height;
	case PARM_TEX_SRC_WIDTH:
		glt = R_GetTexture( arg );
		return glt->srcWidth;
	case PARM_TEX_SRC_HEIGHT:
		glt = R_GetTexture( arg );
		return glt->srcHeight;
	case PARM_TEX_GLFORMAT:
		glt = R_GetTexture( arg );
		return glt->format;
	case PARM_TEX_MIPCOUNT:
		glt = R_GetTexture( arg );
		return glt->numMips;
	case PARM_TEX_DEPTH:
		glt = R_GetTexture( arg );
		return glt->depth;
	case PARM_TEX_SKYBOX:
		Assert( arg >= 0 && arg < 6 );
		return tr.skyboxTextures[arg];
	case PARM_TEX_SKYTEXNUM:
		return tr.skytexturenum;
	case PARM_TEX_LIGHTMAP:
		arg = bound( 0, arg, MAX_LIGHTMAPS - 1 );
		return tr.lightmapTextures[arg];
	case PARM_TEX_TEXNUM:
		glt = R_GetTexture( arg );
		return glt->texnum;
	case PARM_TEX_FLAGS:
		glt = R_GetTexture( arg );
		return glt->flags;
	case PARM_TEX_MEMORY:
		return GL_TexMemory();
	case PARM_LIGHTSTYLEVALUE:
		arg = bound( 0, arg, MAX_LIGHTSTYLES - 1 );
		return tr.lightstylevalue[arg];
	case PARM_REBUILD_GAMMA:
		return glConfig.softwareGammaUpdate;
	case PARM_STENCIL_ACTIVE:
		return glState.stencilEnabled;
	case PARM_TEX_FILTERING:
		if( arg < 0 )
			return gl_texture_nearest.value == 0.0f;

		return GL_TextureFilteringEnabled( R_GetTexture( arg ));
	default:
		return ENGINE_GET_PARM_( parm, arg );
	}
	return 0;
}

static void R_GetDetailScaleForTexture( int texture, float *xScale, float *yScale )
{

}

static void R_GetExtraParmsForTexture( int texture, byte *red, byte *green, byte *blue, byte *density )
{
	gl_texture_t *glt = R_GetTexture( texture );

	if( red ) *red = glt->fogParams[0];
	if( green ) *green = glt->fogParams[1];
	if( blue ) *blue = glt->fogParams[2];
	if( density ) *density = glt->fogParams[3];
}


static void R_SetCurrentEntity( cl_entity_t *ent )
{
	RI.currententity = ent;

	// set model also
	if( RI.currententity != NULL )
	{
		RI.currentmodel = RI.currententity->model;
	}
}

static void R_SetCurrentModel( model_t *mod )
{
	RI.currentmodel = mod;
}

static float R_GetFrameTime( void )
{
	return tr.frametime;
}

static const char *GL_TextureName( unsigned int texnum )
{
	return R_GetTexture( texnum )->name;
}

static const byte *GL_TextureData( unsigned int texnum )
{
	rgbdata_t *pic = R_GetTexture( texnum )->original;

	if( pic != NULL )
		return pic->buffer;
	return NULL;
}

static void R_ProcessEntData( qboolean allocate, cl_entity_t *entities, unsigned int max_entities )
{
	if( !allocate )
	{
		tr.draw_list->num_solid_entities = 0;
		tr.draw_list->num_trans_entities = 0;
		tr.draw_list->num_beam_entities = 0;

		tr.max_entities = 0;
		tr.entities = NULL;
	}
	else
	{
		tr.max_entities = max_entities;
		tr.entities = entities;
	}

	if( gEngfuncs.drawFuncs->R_ProcessEntData )
		gEngfuncs.drawFuncs->R_ProcessEntData( allocate );
}

static void GAME_EXPORT R_Flush( unsigned int flags )
{
	// stub
}

/*
=============
R_SetSkyCloudsTextures

Quake sky cloud texture was processed by the engine,
remember them for easier access during rendering
==============
*/
static void GAME_EXPORT R_SetSkyCloudsTextures( int solidskyTexture, int alphaskyTexture )
{
	tr.solidskyTexture = solidskyTexture;
	tr.alphaskyTexture = alphaskyTexture;
}

/*
===============
R_SetupSky
===============
*/
static void GAME_EXPORT R_SetupSky( int *skyboxTextures )
{
	int i;

	R_UnloadSkybox();

	if( !skyboxTextures )
		return;

	for( i = 0; i < SKYBOX_MAX_SIDES; i++ )
		tr.skyboxTextures[i] = skyboxTextures[i];
}

static qboolean R_SetDisplayTransform( ref_screen_rotation_t rotate, int offset_x, int offset_y, float scale_x, float scale_y )
{
	qboolean ret = true;
	if( rotate > 0 )
	{
		gEngfuncs.Con_Printf("rotation transform not supported\n");
		ret = false;
	}

	if( offset_x || offset_y )
	{
		gEngfuncs.Con_Printf("offset transform not supported\n");
		ret = false;
	}

	if( scale_x != 1.0f || scale_y != 1.0f )
	{
		gEngfuncs.Con_Printf("scale transform not supported\n");
		ret = false;
	}

	return ret;
}

static void GAME_EXPORT VGUI_UploadTextureBlock( int drawX, int drawY, const byte *rgba, int blockWidth, int blockHeight )
{
#if 0
	pglTexSubImage2D( GL_TEXTURE_2D, 0, drawX, drawY, blockWidth, blockHeight, GL_RGBA, GL_UNSIGNED_BYTE, rgba );
#endif
}

static void GAME_EXPORT VGUI_SetupDrawing( qboolean rect )
{
#if 0
	pglEnable( GL_BLEND );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	if( rect )
	{
		pglDisable( GL_ALPHA_TEST );
	}
	else
	{
		pglEnable( GL_ALPHA_TEST );
		pglAlphaFunc( GL_GREATER, 0.0f );
		pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	}
#endif
}

static void GAME_EXPORT R_OverrideTextureSourceSize( unsigned int texnum, uint srcWidth, uint srcHeight )
{
	gl_texture_t *tx = R_GetTexture( texnum );

	tx->srcWidth = srcWidth;
	tx->srcHeight = srcHeight;
}

static void* GAME_EXPORT R_GetProcAddress( const char *name )
{
	return gEngfuncs.GL_GetProcAddress( name );
}

static const char *R_GetConfigName( void )
{
	return "powervr";
}

static const ref_interface_t gReffuncs =
{
	.R_Init = Ref_Init,
	.R_Shutdown = Ref_Shutdown,
	R_GetConfigName,
	R_SetDisplayTransform,

	GL_SetupAttributes,
	GL_InitExtensions,
	GL_ClearExtensions,

	R_GammaChanged,
	R_BeginFrame,
	R_RenderScene,
	R_EndFrame,
	R_PushScene,
	R_PopScene,
	GL_BackendStartFrame,
	GL_BackendEndFrame,

	R_ClearScreen,
	R_AllowFog,
	GL_SetRenderMode,

	R_AddEntity,
	CL_AddCustomBeam,
	R_ProcessEntData,
	R_Flush,

	R_ShowTextures,

	R_GetTextureOriginalBuffer,
	GL_LoadTextureFromBuffer,
	GL_ProcessTexture,
	R_SetupSky,

	R_Set2DMode,
	R_DrawStretchRaw,
	R_DrawStretchPic,
	CL_FillRGBA,
	R_WorldToScreen,

	VID_ScreenShot,
	VID_CubemapShot,

	R_LightPoint,

	R_DecalShoot,
	R_DecalRemoveAll,
	R_CreateDecalList,
	R_ClearAllDecals,

	R_StudioEstimateFrame,
	R_StudioLerpMovement,
	CL_InitStudioAPI,

	R_SetSkyCloudsTextures,
	GL_SubdivideSurface,
	CL_RunLightStyles,

	R_GetSpriteParms,
	R_GetSpriteTexture,

	Mod_ProcessRenderData,
	Mod_StudioLoadTextures,

	CL_DrawParticles,
	CL_DrawTracers,
	CL_DrawBeams,
	R_BeamCull,

	GL_RefGetParm,
	R_GetDetailScaleForTexture,
	R_GetExtraParmsForTexture,
	R_GetFrameTime,

	R_SetCurrentEntity,
	R_SetCurrentModel,

	GL_FindTexture,
	GL_TextureName,
	GL_TextureData,
	GL_LoadTexture,
	GL_CreateTexture,
	GL_LoadTextureArray,
	GL_CreateTextureArray,
	GL_FreeTexture,
	R_OverrideTextureSourceSize,

	DrawSingleDecal,
	R_DecalSetupVerts,
	R_EntityRemoveDecals,

	R_UploadStretchRaw,

	GL_Bind,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	GL_UpdateTexSize,
	NULL,
	NULL,

	CL_DrawParticlesExternal,
	R_LightVec,
	R_StudioGetTexture,

	R_RenderFrame,
	Mod_SetOrthoBounds,
	R_SpeedsMessage,
	Mod_GetCurrentVis,
	R_NewMap,
	R_ClearScene,
	R_GetProcAddress,
	.TriRenderMode = R_TriRenderMode,
	TriBegin,
	TriEnd,
	_TriColor4f,
	_TriColor4ub,
	TriTexCoord2f,
	TriVertex3fv,
	TriVertex3f,
	TriFog,
	R_ScreenToWorld,
	TriGetMatrix,
	TriFogParams,
	TriCullFace,

	VGUI_SetupDrawing,
	VGUI_UploadTextureBlock,
};

int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals );
int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals )
{
	if( version != REF_API_VERSION )
		return 0;

	// fill in our callbacks
	*funcs = gReffuncs;
	gEngfuncs = *engfuncs;
	gpGlobals = globals;

	gp_cl = (ref_client_t *)ENGINE_GET_PARM( PARM_GET_CLIENT_PTR );
	gp_host = (ref_host_t *)ENGINE_GET_PARM( PARM_GET_HOST_PTR );

	return REF_API_VERSION;
}
