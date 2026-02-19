/*
pvr_rmain.c - renderer main loop
Copyright (C) 2026 maximqad

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
#include "xash3d_mathlib.h"
#include "library.h"
#include "beamdef.h"
#include "particledef.h"
#include "entity_types.h"
#include <sh4zam/shz_sh4zam.h>


#define IsLiquidContents( cnt )	( cnt == CONTENTS_WATER || cnt == CONTENTS_SLIME || cnt == CONTENTS_LAVA )

float		gldepthmin, gldepthmax;
ref_instance_t	RI;

// sh4zam world matrix (combined projection * worldview)
float r_world_matrix[16] __attribute__((aligned(32)));
// base screen*proj*view for current frame (column-major)
static float r_viewproj_matrix[16] __attribute__((aligned(32)));
int g_pvr_current_list = -1;
// Quake coordinate system transform matrix (column major)
static const float quake_coord_matrix[16] __attribute__((aligned(8))) = {
     0,  0, -1,  0,
    -1,  0,  0,  0,
     0,  1,  0,  0,
     0,  0,  0,  1
};

extern convar_t gl_clear;

static int R_RankForRenderMode( int rendermode )
{
	switch( rendermode )
	{
	case kRenderTransTexture:
		return 1;	// draw second
	case kRenderTransAdd:
		return 2;	// draw third
	case kRenderGlow:
		return 3;	// must be last!
	}
	return 0;
}

void R_AllowFog( qboolean allowed )
{
	// PVR: fog is a global hardware table, but can be toggled per-poly via cxt.gen.fog_type.
	// We use this as a per-frame/per-draw "allowed" switch (e.g. disable fog for additive passes).
	static qboolean fog_allowed = true;
	fog_allowed = allowed ? true : false;

	// Store in glState for consumers that build polygon contexts.
	glState.isFogEnabled = ( fog_allowed && ( RI.fogEnabled || RI.fogCustom ) && gl_fog.value ) ? 1 : 0;
}

/*
===============
R_OpaqueEntity

Opaque entity can be brush or studio model but sprite
===============
*/
qboolean R_OpaqueEntity( cl_entity_t *ent )
{
	if( R_GetEntityRenderMode( ent ) == kRenderNormal )
	{
		switch( ent->curstate.renderfx )
		{
		case kRenderFxNone:
		case kRenderFxDeadPlayer:
		case kRenderFxLightMultiplier:
		case kRenderFxExplode:
			return true;
		}
	}
	return false;
}

/*
===============
R_TransEntityCompare

Sorting translucent entities by rendermode then by distance
===============
*/
static int R_TransEntityCompare( const void *a, const void *b )
{
	cl_entity_t	*ent1, *ent2;
	vec3_t		vecLen, org;
	float		dist1, dist2;
	int		rendermode1;
	int		rendermode2;

	ent1 = *(cl_entity_t **)a;
	ent2 = *(cl_entity_t **)b;
	rendermode1 = R_GetEntityRenderMode( ent1 );
	rendermode2 = R_GetEntityRenderMode( ent2 );

	// sort by distance
	if( ent1->model->type != mod_brush || rendermode1 != kRenderTransAlpha )
	{
		VectorAverage( ent1->model->mins, ent1->model->maxs, org );
		VectorAdd( ent1->origin, org, org );
		VectorSubtract( RI.vieworg, org, vecLen );
		dist1 = DotProduct( vecLen, vecLen );
	}
	else dist1 = 1000000000;

	if( ent2->model->type != mod_brush || rendermode2 != kRenderTransAlpha )
	{
		VectorAverage( ent2->model->mins, ent2->model->maxs, org );
		VectorAdd( ent2->origin, org, org );
		VectorSubtract( RI.vieworg, org, vecLen );
		dist2 = DotProduct( vecLen, vecLen );
	}
	else dist2 = 1000000000;

	if( dist1 > dist2 )
		return -1;
	if( dist1 < dist2 )
		return 1;

	// then sort by rendermode
	if( R_RankForRenderMode( rendermode1 ) > R_RankForRenderMode( rendermode2 ))
		return 1;
	if( R_RankForRenderMode( rendermode1 ) < R_RankForRenderMode( rendermode2 ))
		return -1;

	return 0;
}

/*
===============
R_WorldToScreen

Convert a given point from world into screen space
Returns true if we behind to screen
===============
*/
int R_WorldToScreen( const vec3_t point, vec3_t screen )
{
	qboolean	behind;
	shz_vec4_t out;

	if( !point || !screen )
		return true;

	// Manual column-major 4x4 matrix-vector multiplication
	// Matrix is column-major: m[0-3] = col0, m[4-7] = col1, m[8-11] = col2, m[12-15] = col3
	const float x = point[0], y = point[1], z = point[2], w = 1.0f;
	const shz_mat4x4_t *m = &RI.worldviewProjectionMatrix;
	out.x = m->elem[0] * x + m->elem[4] * y + m->elem[8] * z + m->elem[12] * w;
	out.y = m->elem[1] * x + m->elem[5] * y + m->elem[9] * z + m->elem[13] * w;
	out.z = m->elem[2] * x + m->elem[6] * y + m->elem[10] * z + m->elem[14] * w;
	out.w = m->elem[3] * x + m->elem[7] * y + m->elem[11] * z + m->elem[15] * w;
	
	screen[0] = out.x;
	screen[1] = out.y;
	screen[2] = 0.0f;

	if( out.w < 0.001f )
	{
		behind = true;
	}
	else
	{
		const float invw = 1.0f / out.w;
		screen[0] *= invw;
		screen[1] *= invw;
		behind = false;
	}

	return behind;
}

/*
===============
R_ScreenToWorld

Convert a given point from screen into world space
===============
*/
void R_ScreenToWorld( const vec3_t screen, vec3_t point )
{
	if( !point || !screen )
		return;

	// TODO: implement a full 4x4 inverse for shz_mat4x4_t if this is needed on DC.
	// Most PVR rendering paths don't need ScreenToWorld right now.
	VectorCopy( screen, point );
}

/*
===============
R_PushScene
===============
*/
void R_PushScene( void )
{
	if( ++tr.draw_stack_pos >= MAX_DRAW_STACK )
		gEngfuncs.Host_Error( "draw stack overflow\n" );

	tr.draw_list = &tr.draw_stack[tr.draw_stack_pos];
}

/*
===============
R_PopScene
===============
*/
void R_PopScene( void )
{
	if( --tr.draw_stack_pos < 0 )
		gEngfuncs.Host_Error( "draw stack underflow\n" );
	tr.draw_list = &tr.draw_stack[tr.draw_stack_pos];
}

/*
===============
R_ClearScene
===============
*/
void R_ClearScene( void )
{
	tr.draw_list->num_solid_entities = 0;
	tr.draw_list->num_trans_entities = 0;
	tr.draw_list->num_beam_entities = 0;

	// clear the scene befor start new frame
	if( gEngfuncs.drawFuncs->R_ClearScene != NULL )
		gEngfuncs.drawFuncs->R_ClearScene();

}

/*
===============
R_AddEntity
===============
*/
qboolean R_AddEntity( struct cl_entity_s *clent, int type )
{
	if( !r_drawentities->value )
		return false; // not allow to drawing

	if( !clent || !clent->model )
		return false; // if set to invisible, skip

	if( FBitSet( clent->curstate.effects, EF_NODRAW ))
		return false; // done

	if( !R_ModelOpaque( clent->curstate.rendermode ) && CL_FxBlend( clent ) <= 0 )
		return true; // invisible

	switch( type )
	{
	case ET_FRAGMENTED:
		r_stats.c_client_ents++;
		break;
	case ET_TEMPENTITY:
		r_stats.c_active_tents_count++;
		break;
	default: break;
	}

	if( R_OpaqueEntity( clent ))
	{
		// opaque
		if( tr.draw_list->num_solid_entities >= MAX_VISIBLE_PACKET )
			return false;

		tr.draw_list->solid_entities[tr.draw_list->num_solid_entities] = clent;
		tr.draw_list->num_solid_entities++;
	}
	else
	{
		// translucent
		if( tr.draw_list->num_trans_entities >= MAX_VISIBLE_PACKET )
			return false;

		tr.draw_list->trans_entities[tr.draw_list->num_trans_entities] = clent;
		tr.draw_list->num_trans_entities++;
	}

	return true;
}

/*
=============
R_Clear
=============
*/
static void R_Clear( int bitMask )
{
	int	bits;
#if 0
	if( ENGINE_GET_PARM( PARM_DEV_OVERVIEW ))
		pglClearColor( 0.0f, 1.0f, 0.0f, 1.0f ); // green background (Valve rules)
	else pglClearColor( 0.5f, 0.5f, 0.5f, 1.0f );

	bits = GL_DEPTH_BUFFER_BIT;

	if( glState.stencilEnabled )
		bits |= GL_STENCIL_BUFFER_BIT;

	bits &= bitMask;

	pglClear( bits );

	// change ordering for overview
	if( RI.drawOrtho )
	{
		gldepthmin = 1.0f;
		gldepthmax = 0.0f;
	}
	else
	{
		gldepthmin = 0.0f;
		gldepthmax = 1.0f;
	}

	pglDepthFunc( GL_LEQUAL );
	pglDepthRange( gldepthmin, gldepthmax );
#endif // PVR CLEAR
}

//=============================================================================
/*
===============
R_GetFarClip
===============
*/
static float R_GetFarClip( void )
{
	if( WORLDMODEL && RI.drawWorld )
		return tr.movevars->zmax * 1.73f;
	return 2048.0f;
}

/*
===============
R_SetupFrustum
===============
*/
void R_SetupFrustum( void )
{
	const ref_overview_t	*ov = gEngfuncs.GetOverviewParms();

	if( RP_NORMALPASS() && ( ENGINE_GET_PARM( PARM_WATER_LEVEL ) >= 3 ) && ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ))
	{
		RI.fov_x = atan( tan( DEG2RAD( RI.fov_x ) / 2 ) * ( 0.97f + sin( gp_cl->time * 1.5f ) * 0.03f )) * 2 / (M_PI_F / 180.0f);
		RI.fov_y = atan( tan( DEG2RAD( RI.fov_y ) / 2 ) * ( 1.03f - sin( gp_cl->time * 1.5f ) * 0.03f )) * 2 / (M_PI_F / 180.0f);
	}

	// build the transformation matrix for the given view angles
	AngleVectors( RI.viewangles, RI.vforward, RI.vright, RI.vup );

	if( !r_lockfrustum.value )
	{
		VectorCopy( RI.vieworg, RI.cullorigin );
		VectorCopy( RI.vforward, RI.cull_vforward );
		VectorCopy( RI.vright, RI.cull_vright );
		VectorCopy( RI.vup, RI.cull_vup );
	}

	if( RI.drawOrtho )
		GL_FrustumInitOrtho( &RI.frustum, ov->xLeft, ov->xRight, ov->yTop, ov->yBottom, ov->zNear, ov->zFar );
	else GL_FrustumInitProj( &RI.frustum, 0.0f, R_GetFarClip(), RI.fov_x, RI.fov_y ); // NOTE: we ignore nearplane here (mirrors only)
}

/*
=============
R_SetupProjectionMatrix
=============
*/
static void R_SetupProjectionMatrix( shz_mat4x4_t *m )
{
	if( RI.drawOrtho )
	{
		// TODO: if overview/ortho is ever needed on DC, implement an ortho builder here.
		shz_mat4x4_init_identity( m );
		return;
	}

	RI.farClip = R_GetFarClip();

	// NOTE: use sh4zam's perspective helper (this was the "good" pre-refactor path).
	// fov is in radians, aspect is w/h, near is fixed to 4.0 like the original code.
	{
		const float aspect = (float)gpGlobals->width / (float)gpGlobals->height;
		const float fov_rad = RI.fov_y * (M_PI_F / 180.0f);
		const float zNear = 4.0f;

		shz_xmtrx_init_identity();
		shz_xmtrx_apply_perspective( fov_rad, aspect, zNear );
		shz_xmtrx_store_4x4( m );
	}
}

/*
=============
R_SetupModelviewMatrix
=============
*/
static void R_SetupModelviewMatrix( shz_mat4x4_t *m )
{
	const float roll  = -RI.viewangles[2] * (M_PI_F / 180.0f);
	const float pitch = -RI.viewangles[0] * (M_PI_F / 180.0f);
	const float yaw   = -RI.viewangles[1] * (M_PI_F / 180.0f);

	shz_xmtrx_init_identity();
	shz_xmtrx_apply_4x4((const shz_mat4x4_t*)quake_coord_matrix);
	shz_xmtrx_rotate_x( roll );
	shz_xmtrx_rotate_y( pitch );
	shz_xmtrx_rotate_z( yaw );
	shz_xmtrx_translate( -RI.vieworg[0], -RI.vieworg[1], -RI.vieworg[2] );
	shz_xmtrx_store_4x4( m );
}

/*
=============
R_LoadIdentity
=============
*/
void R_LoadIdentity( void )
{
	if( tr.modelviewIdentity ) return;
	shz_mat4x4_init_identity( &RI.objectMatrix );
	RI.modelviewMatrix = RI.worldviewMatrix;
	memcpy( r_world_matrix, r_viewproj_matrix, sizeof( r_viewproj_matrix ));
	tr.modelviewIdentity = true;
}

/*
=============
R_RotateForEntity
=============
*/
void R_RotateForEntity( cl_entity_t *e )
{
	float	scale = 1.0f;
	const float roll  = e->angles[2] * (M_PI_F / 180.0f);
	const float pitch = e->angles[0] * (M_PI_F / 180.0f);
	const float yaw   = e->angles[1] * (M_PI_F / 180.0f);

	if( e == CL_GetEntityByIndex( 0 ))
	{
		R_LoadIdentity();
		return;
	}

	if( e->model->type != mod_brush && e->curstate.scale > 0.0f )
		scale = e->curstate.scale;

	// Build object matrix in column-major using sh4zam.

	shz_xmtrx_init_identity();
	// IMPORTANT: xmtrx_* are applied as post-multiplies (M = M * X).
	// For a correct object transform we want: M = T * R * S
	// so scale/rotation happen in local space, then translation places it in world.
	shz_xmtrx_translate( e->origin[0], e->origin[1], e->origin[2] );
    // Match the existing world/view convention (Z yaw, Y pitch, X roll)
	shz_xmtrx_rotate_z( yaw );
	shz_xmtrx_rotate_y( pitch );
	shz_xmtrx_rotate_x( roll );

	if( scale != 1.0f )
		shz_xmtrx_apply_scale( scale, scale, scale );

	shz_xmtrx_store_4x4( &RI.objectMatrix );

	shz_mat4x4_mult( &RI.modelviewMatrix, &RI.worldviewMatrix, &RI.objectMatrix );
	tr.modelviewIdentity = false;

	// Update PVR transform for this entity: viewproj * object
	shz_xmtrx_load_4x4((shz_mat4x4_t*)r_viewproj_matrix);
	shz_xmtrx_apply_4x4(&RI.objectMatrix);
	shz_xmtrx_store_4x4((shz_mat4x4_t*)r_world_matrix);
}

/*
=============
R_TranslateForEntity
=============
*/
void R_TranslateForEntity( cl_entity_t *e )
{
	float	scale = 1.0f;

	if( e == CL_GetEntityByIndex( 0 ))
	{
		R_LoadIdentity();
		return;
	}

	if( e->model->type != mod_brush && e->curstate.scale > 0.0f )
		scale = e->curstate.scale;

	shz_xmtrx_init_identity();
	shz_xmtrx_translate( e->origin[0], e->origin[1], e->origin[2] );
	if( scale != 1.0f )
		shz_xmtrx_apply_scale( scale, scale, scale );
	shz_xmtrx_store_4x4( &RI.objectMatrix );

	shz_mat4x4_mult( &RI.modelviewMatrix, &RI.worldviewMatrix, &RI.objectMatrix );
	tr.modelviewIdentity = false;

	// Update PVR transform for this entity: viewproj * object
	shz_xmtrx_load_4x4((shz_mat4x4_t*)r_viewproj_matrix);
	shz_xmtrx_apply_4x4(&RI.objectMatrix);
	shz_xmtrx_store_4x4((shz_mat4x4_t*)r_world_matrix);
}

/*
===============
R_FindViewLeaf
===============
*/
void R_FindViewLeaf( void )
{
	RI.oldviewleaf = RI.viewleaf;
	RI.viewleaf = gEngfuncs.Mod_PointInLeaf( RI.pvsorigin, WORLDMODEL->nodes );
}

/*
===============
R_SetupFrame
===============
*/
static void R_SetupFrame( void )
{
	// setup viewplane dist
	RI.viewplanedist = DotProduct( RI.vieworg, RI.vforward );

	// PVR: fog state is derived from RI.fogEnabled/RI.fogCustom, not queried from GL.
	// glState.isFogEnabled is updated in R_CheckFog() and R_AllowFog().

	if( !gl_nosort.value )
	{
		// sort translucents entities by rendermode and distance
		qsort( tr.draw_list->trans_entities, tr.draw_list->num_trans_entities, sizeof( cl_entity_t* ), R_TransEntityCompare );
	}
	// current viewleaf
	if( RI.drawWorld )
	{
		RI.isSkyVisible = false; // unknown at this moment
		R_FindViewLeaf();
	}
}

/*
=============
R_SetupGL
=============
*/
void R_SetupGL( qboolean set_gl_state )
{
	const float screen_width = (float)gpGlobals->width;
	const float screen_height = (float)gpGlobals->height;
	// Build RI matrices (now sh4zam column-major) using sh4zam only.
	R_SetupModelviewMatrix( &RI.worldviewMatrix );
	R_SetupProjectionMatrix( &RI.projectionMatrix );
	shz_mat4x4_mult( &RI.worldviewProjectionMatrix, &RI.projectionMatrix, &RI.worldviewMatrix );

	shz_xmtrx_init_identity();
	shz_xmtrx_apply_screen( screen_width, screen_height );
	shz_xmtrx_apply_4x4( &RI.projectionMatrix );
	shz_xmtrx_apply_4x4( &RI.worldviewMatrix );
	shz_xmtrx_store_4x4((shz_mat4x4_t*)r_viewproj_matrix);
	memcpy( r_world_matrix, r_viewproj_matrix, sizeof( r_viewproj_matrix ));

	if( !set_gl_state ) return;

}

/*
=============
R_EndGL
=============
*/
static void R_EndGL( void )
{

}

/*
=============
R_RecursiveFindWaterTexture

using to find source waterleaf with
watertexture to grab fog values from it
=============
*/
static gl_texture_t *R_RecursiveFindWaterTexture( const mnode_t *node, const mnode_t *ignore, qboolean down )
{
	gl_texture_t *tex = NULL;
	mnode_t *children[2];

	// assure the initial node is not null
	// we could check it here, but we would rather check it
	// outside the call to get rid of one additional recursion level
	Assert( node != NULL );

	// ignore solid nodes
	if( node->contents == CONTENTS_SOLID )
		return NULL;

	if( node->contents < 0 )
	{
		mleaf_t		*pleaf;
		msurface_t	**mark;
		int		i, c;

		// ignore non-liquid leaves
		if( node->contents != CONTENTS_WATER && node->contents != CONTENTS_LAVA && node->contents != CONTENTS_SLIME )
			 return NULL;

		// find texture
		pleaf = (mleaf_t *)node;
		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		for( i = 0; i < c; i++, mark++ )
		{
			if( (*mark)->flags & SURF_DRAWTURB && (*mark)->texinfo && (*mark)->texinfo->texture )
				return R_GetTexture( (*mark)->texinfo->texture->gl_texturenum );
		}

		// texture not found
		return NULL;
	}

	// this is a regular node
	// traverse children
	node_children( children, node, WORLDMODEL );

	if( children[0] && ( children[0] != ignore ))
	{
		tex = R_RecursiveFindWaterTexture( children[0], node, true );
		if( tex ) return tex;
	}

	if( children[1] && ( children[1] != ignore ))
	{
		tex = R_RecursiveFindWaterTexture( children[1], node, true );
		if( tex )	return tex;
	}

	// for down recursion, return immediately
	if( down ) return NULL;

	// texture not found, step up if any
	if( node->parent )
		return R_RecursiveFindWaterTexture( node->parent, node, false );

	// top-level node, bail out
	return NULL;
}

/*
=============
R_CheckFog

check for underwater fog
Using backward recursion to find waterline leaf
from underwater leaf (idea: XaeroX)
=============
*/
static void R_CheckFog( void )
{
	cl_entity_t	*ent;
	gl_texture_t	*tex;
	int		i, cnt, count;

	// Default: no fog unless enabled below.
	RI.fogEnabled = false;
	RI.fogSkybox = true;

	// quake global fog
	if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ))
	{
		if( !tr.movevars->fog_settings )
		{
			glState.isFogEnabled = false;
			return;
		}

		// quake-style global fog
		RI.fogColor[0] = ((tr.movevars->fog_settings & 0xFF000000) >> 24) / 255.0f;
		RI.fogColor[1] = ((tr.movevars->fog_settings & 0xFF0000) >> 16) / 255.0f;
		RI.fogColor[2] = ((tr.movevars->fog_settings & 0xFF00) >> 8) / 255.0f;
		RI.fogDensity = ((tr.movevars->fog_settings & 0xFF) / 255.0f) * 0.01f;
		RI.fogStart = RI.fogEnd = 0.0f;
		RI.fogColor[3] = 1.0f;
		RI.fogCustom = false;
		RI.fogEnabled = true;
		RI.fogSkybox = true;
		return;
	}

	if( RI.onlyClientDraw || ENGINE_GET_PARM( PARM_WATER_LEVEL ) < 3 || !RI.drawWorld || !RI.viewleaf )
	{
		if( RI.cached_waterlevel == 3 )
		{
			// in some cases waterlevel jumps from 3 to 1. Catch it
			RI.cached_waterlevel = ENGINE_GET_PARM( PARM_WATER_LEVEL );
			RI.cached_contents = CONTENTS_EMPTY;
			if( !RI.fogCustom )
			{
				glState.isFogEnabled = false;
			}
		}
		return;
	}

	ent = gEngfuncs.CL_GetWaterEntity( RI.vieworg );
	if( ent && ent->model && ent->model->type == mod_brush && ent->curstate.skin < 0 )
		cnt = ent->curstate.skin;
	else cnt = RI.viewleaf->contents;

	RI.cached_waterlevel = ENGINE_GET_PARM( PARM_WATER_LEVEL );

	if( !IsLiquidContents( RI.cached_contents ) && IsLiquidContents( cnt ))
	{
		tex = NULL;

		// check for water texture
		if( ent && ent->model && ent->model->type == mod_brush )
		{
			msurface_t	*surf;

			count = ent->model->nummodelsurfaces;

			for( i = 0, surf = &ent->model->surfaces[ent->model->firstmodelsurface]; i < count; i++, surf++ )
			{
				if( surf->flags & SURF_DRAWTURB && surf->texinfo && surf->texinfo->texture )
				{
					tex = R_GetTexture( surf->texinfo->texture->gl_texturenum );
					RI.cached_contents = ent->curstate.skin;
					break;
				}
			}
		}
		else
		{
			tex = R_RecursiveFindWaterTexture( RI.viewleaf->parent, NULL, false );
			if( tex ) RI.cached_contents = RI.viewleaf->contents;
		}

		if( !tex ) return;	// no valid fogs

		// copy fog params
		RI.fogColor[0] = tex->fogParams[0] / 255.0f;
		RI.fogColor[1] = tex->fogParams[1] / 255.0f;
		RI.fogColor[2] = tex->fogParams[2] / 255.0f;
		RI.fogDensity = tex->fogParams[3] * 0.000025f;
		RI.fogStart = RI.fogEnd = 0.0f;
		RI.fogColor[3] = 1.0f;
		RI.fogCustom = false;
		RI.fogEnabled = true;
		RI.fogSkybox = true;
	}
	else
	{
		RI.fogCustom = false;
		RI.fogEnabled = true;
		RI.fogSkybox = true;
	}

	// Keep glState in sync for context builders.
	glState.isFogEnabled = (( RI.fogEnabled || RI.fogCustom ) && gl_fog.value) ? 1 : 0;
}

/*
=============
R_CheckGLFog

special condition for Spirit 1.9
that used direct calls of glFog-functions

PVR: This compatibility hack is not applicable since we can't query GL state.
Mods that call GL fog functions directly won't work with PVR hardware fog.
=============
*/
static void R_CheckGLFog( void )
{
	// PVR: No-op. We can't query GL fog state, and fog is managed via RI.fogEnabled/RI.fogCustom.
	// Mods that call pglFog* directly won't have their fog state reflected here.
}

/*
=============
R_DrawFog

=============
*/
void R_DrawFog( void )
{
	if( !RI.fogEnabled || !gl_fog.value )
		return;

	// Configure PVR hardware table fog. Color is 0..1.
	static vec4_t last_color = { -1, -1, -1, -1 };
	static float last_density = -1.0f;
	static float last_start = -1.0f, last_end = -1.0f;
	static int last_mode = -1;

	// mode: 0 = exp2, 1 = exp, 2 = linear
	int mode = 0;
	if( RI.fogCustom && ( RI.fogEnd > RI.fogStart ))
		mode = ( RI.fogDensity > 0.0f ) ? 0 : 2;
	else
		mode = ( RI.fogDensity > 0.0f ) ? 0 : 1;

	if( last_mode != mode ||
	    last_density != RI.fogDensity ||
	    last_start != RI.fogStart ||
	    last_end != RI.fogEnd ||
	    last_color[0] != RI.fogColor[0] || last_color[1] != RI.fogColor[1] ||
	    last_color[2] != RI.fogColor[2] || last_color[3] != RI.fogColor[3] )
	{
		pvr_fog_table_color( 1.0f, RI.fogColor[0], RI.fogColor[1], RI.fogColor[2] );

		if( mode == 2 )
			pvr_fog_table_linear( RI.fogStart, RI.fogEnd );
		else if( mode == 1 )
			pvr_fog_table_exp( RI.fogDensity );
		else
			pvr_fog_table_exp2( RI.fogDensity );

		Vector4Copy( RI.fogColor, last_color );
		last_density = RI.fogDensity;
		last_start = RI.fogStart;
		last_end = RI.fogEnd;
		last_mode = mode;
	}
}

/*
=============
R_DrawOpaqueEntities
=============
*/
static void R_DrawOpaqueEntities( void )
{
	int	i;

	tr.blend = 1.0f;

	// solid entities (brush + studio) only
	for( i = 0; i < tr.draw_list->num_solid_entities && !RI.onlyClientDraw; i++ )
	{
		RI.currententity = tr.draw_list->solid_entities[i];
		RI.currentmodel = RI.currententity->model;

		Assert( RI.currententity != NULL );
		Assert( RI.currentmodel != NULL );

		switch( RI.currentmodel->type )
		{
		case mod_brush:
			R_DrawBrushModel( RI.currententity );
			break;
		case mod_studio:
			R_DrawStudioModel( RI.currententity );
			break;
		default:
			break;
		}
	}

	// client-side effects: opaque pass
	if( !RI.onlyClientDraw )
		gEngfuncs.CL_DrawEFX( tr.frametime, false );

	// TriAPI normal triangles (opaque)
	if( RI.drawWorld )
		gEngfuncs.pfnDrawNormalTriangles();

	// Viewmodel is effectively opaque and should be drawn in OP.
	// Keep it here to avoid any list switching.
	if( !RI.onlyClientDraw )
		R_DrawViewModel();
}

/*
=============
R_DrawTranslucentEntities
=============
*/
static void R_DrawTranslucentEntities( void )
{
	int	i;

	// sprites (from solid list) go to TR because they blend
	for( i = 0; i < tr.draw_list->num_solid_entities && !RI.onlyClientDraw; i++ )
	{
		RI.currententity = tr.draw_list->solid_entities[i];
		RI.currentmodel = RI.currententity->model;

		Assert( RI.currententity != NULL );
		Assert( RI.currentmodel != NULL );

		if( RI.currentmodel->type == mod_sprite )
			R_DrawSpriteModel( RI.currententity );
	}

	// translucent entities
	for( i = 0; i < tr.draw_list->num_trans_entities && !RI.onlyClientDraw; i++ )
	{
		RI.currententity = tr.draw_list->trans_entities[i];
		RI.currentmodel = RI.currententity->model;

		// handle custom rendermodes
		if( RI.currententity->curstate.rendermode != kRenderNormal )
			tr.blend = CL_FxBlend( RI.currententity ) / 255.0f;
		else tr.blend = 1.0f;

		if( tr.blend <= 0.0f )
			continue;

		Assert( RI.currententity != NULL );
		Assert( RI.currentmodel != NULL );

		switch( RI.currentmodel->type )
		{
		case mod_brush:
			R_DrawBrushModel( RI.currententity );
			break;
		case mod_studio:
			R_DrawStudioModel( RI.currententity );
			break;
		case mod_sprite:
			R_DrawSpriteModel( RI.currententity );
			break;
		default:
			break;
		}
	}

	// TriAPI transparent triangles
	if( RI.drawWorld )
		gEngfuncs.pfnDrawTransparentTriangles();

	// client-side effects: translucent pass
	if( !RI.onlyClientDraw )
	{
		R_AllowFog( false );
		gEngfuncs.CL_DrawEFX( tr.frametime, true );
		R_AllowFog( true );
	}
}

/*
================
R_RenderScene

R_SetupRefParams must be called right before
================
*/
void R_RenderScene( void )
{
	if( !WORLDMODEL && RI.drawWorld )
		gEngfuncs.Host_Error( "%s: NULL worldmodel\n", __func__ );

	// frametime is valid only for normal pass
	if( RP_NORMALPASS( ))
		tr.frametime = gp_cl->time -   gp_cl->oldtime;
	else tr.frametime = 0.0;

	// begin a new frame
	tr.framecount++;

	R_PushDlights();

	R_SetupFrustum();
	R_SetupFrame();
	R_SetupGL( true );
	R_Clear( ~0 );

	R_MarkLeaves();
	R_DrawFog ();
	if( RI.drawWorld )
		R_AnimateRipples();

	R_CheckGLFog();
	// submit opaque geometry into OP list.
	g_pvr_current_list = PVR_LIST_OP_POLY;
	pvr_list_begin( PVR_LIST_OP_POLY );
	R_DrawWorld();
	R_DrawOpaqueEntities();
	pvr_list_finish();
	g_pvr_current_list = -1;
	
	// submit tr geom (translucent entities, sprites, etc.)
	g_pvr_current_list = PVR_LIST_TR_POLY;
	pvr_list_begin( PVR_LIST_TR_POLY );
	DrawDecalsBatch();
	R_DrawTranslucentEntities();
	R_DrawWaterSurfaces();
	pvr_list_finish();
	g_pvr_current_list = -1;

	R_CheckFog();

	gEngfuncs.CL_ExtraUpdate ();	// don't let sound get messed up if going slow
	R_EndGL();
}

void R_GammaChanged( qboolean do_reset_gamma )
{
	// PVR uses vertex lighting with gouraud shading, not lightmap textures
	// Gamma tables are updated by engine, we just need to mark gamma changed
	// so vertex colors will use updated gamma tables on next render
	glConfig.softwareGammaUpdate = true;
	
	// No need to rebuild lightmaps - we sample vertex lights directly
	// Gamma correction is applied per-vertex in SampleVertexLight()
	
	glConfig.softwareGammaUpdate = false;
}

static void R_CheckCvars( void )
{
	qboolean rebuild = false;

	if( FBitSet( gl_overbright.flags, FCVAR_CHANGED ))
	{
		ClearBits( gl_overbright.flags, FCVAR_CHANGED );
		rebuild = true;
	}


	if( rebuild )
		R_GammaChanged( false );
}

/*
===============
R_BeginFrame
===============
*/
void R_BeginFrame( qboolean clearScene )
{
	glConfig.softwareGammaUpdate = false;	// in case of possible fails

	R_CheckCvars();
	pvr_wait_ready();
    pvr_scene_begin();

	// update texture parameters
	if( FBitSet( gl_texture_nearest.flags|gl_lightmap_nearest.flags|gl_texture_anisotropy.flags|gl_texture_lodbias.flags, FCVAR_CHANGED ))
		R_SetTextureParameters();

	gEngfuncs.CL_ExtraUpdate ();
}

/*
===============
R_SetupRefParams

set initial params for renderer
===============
*/
void R_SetupRefParams( const ref_viewpass_t *rvp )
{
	RI.params = RP_NONE;
	RI.drawWorld = FBitSet( rvp->flags, RF_DRAW_WORLD );
	RI.onlyClientDraw = FBitSet( rvp->flags, RF_ONLY_CLIENTDRAW );
	RI.farClip = 0;

	if( !FBitSet( rvp->flags, RF_DRAW_CUBEMAP ))
		RI.drawOrtho = FBitSet( rvp->flags, RF_DRAW_OVERVIEW );
	else RI.drawOrtho = false;

	// setup viewport
	RI.viewport[0] = rvp->viewport[0];
	RI.viewport[1] = rvp->viewport[1];
	RI.viewport[2] = rvp->viewport[2];
	RI.viewport[3] = rvp->viewport[3];

	// calc FOV
	RI.fov_x = rvp->fov_x;
	RI.fov_y = rvp->fov_y;

	VectorCopy( rvp->vieworigin, RI.vieworg );
	VectorCopy( rvp->viewangles, RI.viewangles );
	VectorCopy( rvp->vieworigin, RI.pvsorigin );
}

/*
===============
R_RenderFrame
===============
*/
void R_RenderFrame( const ref_viewpass_t *rvp )
{
	if( r_norefresh->value )
		return;

	// setup the initial render params
	R_SetupRefParams( rvp );

	// completely override rendering
	if( gEngfuncs.drawFuncs->GL_RenderFrame != NULL )
	{
		tr.fCustomRendering = true;

		if( gEngfuncs.drawFuncs->GL_RenderFrame( rvp ))
		{
			R_GatherPlayerLight();
			tr.realframecount++;
			tr.fResetVis = true;
			return;
		}
	}

	tr.fCustomRendering = false;
	if( !RI.onlyClientDraw )
		R_RunViewmodelEvents();

	tr.realframecount++; // right called after viewmodel events
	R_RenderScene();

	return;
}

/*
===============
R_EndFrame
===============
*/
void R_EndFrame( void )
{
	R_Set2DMode( false );	
	pvr_scene_finish();
}

/*
===============
R_DrawCubemapView
===============
*/
void R_DrawCubemapView( const vec3_t origin, const vec3_t angles, int size )
{
	ref_viewpass_t rvp;

	// basic params
	rvp.flags = rvp.viewentity = 0;
	SetBits( rvp.flags, RF_DRAW_WORLD );
	SetBits( rvp.flags, RF_DRAW_CUBEMAP );

	rvp.viewport[0] = rvp.viewport[1] = 0;
	rvp.viewport[2] = rvp.viewport[3] = size;
	rvp.fov_x = rvp.fov_y = 90.0f; // this is a final fov value

	// setup origin & angles
	VectorCopy( origin, rvp.vieworigin );
	VectorCopy( angles, rvp.viewangles );

	R_RenderFrame( &rvp );

	RI.viewleaf = NULL;		// force markleafs next frame
}

/*
===============
CL_FxBlend
===============
*/
int CL_FxBlend( cl_entity_t *e )
{
	int	blend = 0;
	float	offset, dist;
	vec3_t	tmp;

	offset = ((int)e->index ) * 363.0f; // Use ent index to de-sync these fx

	switch( e->curstate.renderfx )
	{
	case kRenderFxPulseSlowWide:
		blend = e->curstate.renderamt + 0x40 * sin( gp_cl->time * 2 + offset );
		break;
	case kRenderFxPulseFastWide:
		blend = e->curstate.renderamt + 0x40 * sin( gp_cl->time * 8 + offset );
		break;
	case kRenderFxPulseSlow:
		blend = e->curstate.renderamt + 0x10 * sin( gp_cl->time * 2 + offset );
		break;
	case kRenderFxPulseFast:
		blend = e->curstate.renderamt + 0x10 * sin( gp_cl->time * 8 + offset );
		break;
	case kRenderFxFadeSlow:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt > 0 )
				e->curstate.renderamt -= 1;
			else e->curstate.renderamt = 0;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxFadeFast:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt > 3 )
				e->curstate.renderamt -= 4;
			else e->curstate.renderamt = 0;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidSlow:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt < 255 )
				e->curstate.renderamt += 1;
			else e->curstate.renderamt = 255;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidFast:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt < 252 )
				e->curstate.renderamt += 4;
			else e->curstate.renderamt = 255;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeSlow:
		blend = 20 * sin( gp_cl->time * 4 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeFast:
		blend = 20 * sin( gp_cl->time * 16 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeFaster:
		blend = 20 * sin( gp_cl->time * 36 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxFlickerSlow:
		blend = 20 * (sin( gp_cl->time * 2 ) + sin( gp_cl->time * 17 + offset ));
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxFlickerFast:
		blend = 20 * (sin( gp_cl->time * 16 ) + sin( gp_cl->time * 23 + offset ));
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxHologram:
	case kRenderFxDistort:
		VectorCopy( e->origin, tmp );
		VectorSubtract( tmp, RI.vieworg, tmp );
		dist = DotProduct( tmp, RI.vforward );

		// turn off distance fade
		if( e->curstate.renderfx == kRenderFxDistort )
			dist = 1;

		if( dist <= 0 )
		{
			blend = 0;
		}
		else
		{
			e->curstate.renderamt = 180;
			if( dist <= 100 ) blend = e->curstate.renderamt;
			else blend = (int) ((1.0f - ( dist - 100 ) * ( 1.0f / 400.0f )) * e->curstate.renderamt );
			blend += gEngfuncs.COM_RandomLong( -32, 31 );
		}
		break;
	default:
		blend = e->curstate.renderamt;
		break;
	}

	blend = bound( 0, blend, 255 );

	return blend;
}
