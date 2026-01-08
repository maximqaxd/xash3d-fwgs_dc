/*
pvr_sprite.c - sprite rendering
Copyright (C) 2025 maximqad

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
#include "pvr_clip.h"
#include "pm_local.h"
#include "sprite.h"
#include "studio.h"
#include "entity_types.h"

#define GLARE_FALLOFF	19000.0f

char		sprite_name[MAX_QPATH];
char		group_suffix[8];
static uint	r_texFlags = 0;
static int	sprite_version;
float		sprite_radius;

/*
====================
R_SpriteInit

====================
*/
void R_SpriteInit( void )
{
}

/*
====================
R_SpriteLoadFrame

upload a single frame
====================
*/
static const byte *R_SpriteLoadFrame( model_t *mod, const void *pin, mspriteframe_t **ppframe, int num )
{
	dspriteframe_t	pinframe;
	mspriteframe_t	*pspriteframe;
	int		gl_texturenum = 0;
	char		texname[128];
	int		bytes = 1;

	memcpy( &pinframe, pin, sizeof( dspriteframe_t ));

	if( sprite_version == SPRITE_VERSION_32 )
		bytes = 4;

	// build uinque frame name
	if( FBitSet( mod->flags, MODEL_CLIENT )) // it's a HUD sprite
	{
		Q_snprintf( texname, sizeof( texname ), "#HUD/%s(%s:%i%i).spr", sprite_name, group_suffix, num / 10, num % 10 );
		gl_texturenum = GL_LoadTexture( texname, pin, pinframe.width * pinframe.height * bytes, r_texFlags );
	}

	else
	{
		if( gl_texturenum == 0 )
		{
			Q_snprintf( texname, sizeof( texname ), "#%s(%s:%i%i).spr", sprite_name, group_suffix, num / 10, num % 10 );
			gl_texturenum = GL_LoadTexture( texname, pin, pinframe.width * pinframe.height * bytes, r_texFlags );
		}
	}


	// setup frame description
	pspriteframe = Mem_Malloc( mod->mempool, sizeof( mspriteframe_t ));
	pspriteframe->width = pinframe.width;
	pspriteframe->height = pinframe.height;
	pspriteframe->up = pinframe.origin[1];
	pspriteframe->left = pinframe.origin[0];
	pspriteframe->down = pinframe.origin[1] - pinframe.height;
	pspriteframe->right = pinframe.width + pinframe.origin[0];
	pspriteframe->gl_texturenum = gl_texturenum;
	*ppframe = pspriteframe;

	return (( const byte* )pin + sizeof( dspriteframe_t ) + pinframe.width * pinframe.height * bytes );
}

/*
====================
R_SpriteLoadGroup

upload a group frames
====================
*/
static const byte *R_SpriteLoadGroup( model_t *mod, const void *pin, mspriteframe_t **ppframe, int framenum )
{
	const dspritegroup_t	*pingroup;
	mspritegroup_t	*pspritegroup;
	const dspriteinterval_t	*pin_intervals;
	float		*poutintervals;
	int		i, groupsize, numframes;
	const void		*ptemp;

	pingroup = (const dspritegroup_t *)pin;
	numframes = pingroup->numframes;

	groupsize = sizeof( mspritegroup_t ) + (numframes - 1) * sizeof( pspritegroup->frames[0] );
	pspritegroup = Mem_Calloc( mod->mempool, groupsize );
	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;
	pin_intervals = (const dspriteinterval_t *)(pingroup + 1);
	poutintervals = Mem_Calloc( mod->mempool, numframes * sizeof( float ));
	pspritegroup->intervals = poutintervals;

	for( i = 0; i < numframes; i++ )
	{
		*poutintervals = pin_intervals->interval;
		if( *poutintervals <= 0.0f )
			*poutintervals = 1.0f; // set error value
		poutintervals++;
		pin_intervals++;
	}

	ptemp = (const void *)pin_intervals;
	for( i = 0; i < numframes; i++ )
	{
		ptemp = R_SpriteLoadFrame( mod, ptemp, &pspritegroup->frames[i], framenum * 10 + i );
	}

	return ptemp;
}


/*
====================
Mod_LoadSpriteModel

load sprite model
====================
*/
void Mod_LoadSpriteModel( model_t *mod, const void *buffer, qboolean *loaded, uint texFlags )
{
	const dsprite_t *pin;
	const short     *numi = NULL;
	const byte      *pframetype;
	msprite_t       *psprite;
	int i;

	pin = buffer;
	psprite = mod->cache.data;

	if( pin->version == SPRITE_VERSION_Q1 || pin->version == SPRITE_VERSION_32 )
		numi = NULL;
	else if( pin->version == SPRITE_VERSION_HL )
		numi = (const short *)((const byte *)buffer + sizeof( dsprite_hl_t ));

	r_texFlags = texFlags;
	sprite_version = pin->version;
	Q_strncpy( sprite_name, mod->name, sizeof( sprite_name ));
	COM_StripExtension( sprite_name );

	if( numi == NULL )
	{
		rgbdata_t	*pal;

		pal = gEngfuncs.FS_LoadImage( "#id.pal", (byte *)&i, 768 );
		pframetype = ((const byte*)buffer + sizeof( dsprite_q1_t )); // pinq1 + 1
		gEngfuncs.FS_FreeImage( pal ); // palette installed, no reason to keep this data
	}
	else if( *numi <= 256 )
	{
		const byte	*src = (const byte *)(numi+1);
		rgbdata_t	*pal;
		size_t pal_bytes = *numi * 3;

		// install palette
		switch( psprite->texFormat )
		{
		case SPR_INDEXALPHA:
			pal = gEngfuncs.FS_LoadImage( "#gradient.pal", src, pal_bytes );
			break;
		case SPR_ALPHTEST:
			pal = gEngfuncs.FS_LoadImage( "#masked.pal", src, pal_bytes );
			break;
		default:
			pal = gEngfuncs.FS_LoadImage( "#normal.pal", src, pal_bytes );
			break;
		}

		pframetype = (const byte *)(src + pal_bytes);
		gEngfuncs.FS_FreeImage( pal ); // palette installed, no reason to keep this data
	}
	else
	{
		gEngfuncs.Con_DPrintf( S_ERROR "%s has wrong number of palette colors %i (should be less or equal than 256)\n", mod->name, *numi );
		return;
	}

	if( mod->numframes < 1 )
		return;

	for( i = 0; i < mod->numframes; i++ )
	{
		frametype_t frametype;
		dframetype_t dframetype;

		memcpy( &dframetype, pframetype, sizeof( dframetype ));
		frametype = dframetype.type;
		psprite->frames[i].type = (spriteframetype_t)frametype;

		switch( frametype )
		{
		case FRAME_SINGLE:
			Q_strncpy( group_suffix, "frame", sizeof( group_suffix ));
			pframetype = R_SpriteLoadFrame( mod, pframetype + sizeof( dframetype_t ), &psprite->frames[i].frameptr, i );
			break;
		case FRAME_GROUP:
			Q_strncpy( group_suffix, "group", sizeof( group_suffix ));
			pframetype = R_SpriteLoadGroup( mod, pframetype + sizeof( dframetype_t ), &psprite->frames[i].frameptr, i );
			break;
		case FRAME_ANGLED:
			Q_strncpy( group_suffix, "angle", sizeof( group_suffix ));
			pframetype = R_SpriteLoadGroup( mod, pframetype + sizeof( dframetype_t ), &psprite->frames[i].frameptr, i );
			break;
		}
		if( pframetype == NULL ) break; // technically an error
	}

	if( loaded ) *loaded = true;	// done
}

/*
====================
Mod_UnloadSpriteModel

release sprite model and frames
====================
*/
void Mod_SpriteUnloadTextures( void *data )
{
	msprite_t *psprite = data;
	int i;

	if( !data )
		return;

	// release all textures
	for( i = 0; i < psprite->numframes; i++ )
	{
		if( !psprite->frames[i].frameptr )
			continue;

		if( psprite->frames[i].type == SPR_SINGLE )
		{
			GL_FreeTexture( psprite->frames[i].frameptr->gl_texturenum );
		}
		else
		{
			mspritegroup_t *pspritegroup = (mspritegroup_t *)psprite->frames[i].frameptr;
			int j;

			for( j = 0; j < pspritegroup->numframes; j++ )
			{
				if( pspritegroup->frames[j] )
					GL_FreeTexture( pspritegroup->frames[j]->gl_texturenum );
			}
		}
	}
}

/*
================
R_GetSpriteFrame

assume pModel is valid
================
*/
mspriteframe_t *R_GetSpriteFrame( const model_t *pModel, int frame, float yaw )
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	mspriteframe_t	*pspriteframe = NULL;
	float		*pintervals, fullinterval;
	int		i, numframes;
	float		targettime;

	Assert( pModel != NULL );
	psprite = pModel->cache.data;

	if( frame < 0 )
	{
		frame = 0;
	}
	else if( frame >= psprite->numframes )
	{
		if( frame > psprite->numframes )
			gEngfuncs.Con_Printf( S_WARN "%s: no such frame %d (%s)\n", __func__, frame, pModel->name );
		frame = psprite->numframes - 1;
	}

	if( psprite->frames[frame].type == SPR_SINGLE )
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}
	else if( psprite->frames[frame].type == SPR_GROUP )
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];

		// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
		// are positive, so we don't have to worry about division by zero
		targettime = gp_cl->time - ((int)( gp_cl->time / fullinterval )) * fullinterval;

		for( i = 0; i < (numframes - 1); i++ )
		{
			if( pintervals[i] > targettime )
				break;
		}
		pspriteframe = pspritegroup->frames[i];
	}
	else if( psprite->frames[frame].type == FRAME_ANGLED )
	{
		int	angleframe = (int)(Q_rint(( RI.viewangles[1] - yaw + 45.0f ) / 360 * 8) - 4) & 7;

		// e.g. doom-style sprite monsters
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pspriteframe = pspritegroup->frames[angleframe];
	}

	return pspriteframe;
}

/*
================
R_GetSpriteFrameInterpolant

NOTE: we using prevblending[0] and [1] for holds interval
between frames where are we lerping
================
*/
static float R_GetSpriteFrameInterpolant( cl_entity_t *ent, mspriteframe_t **oldframe, mspriteframe_t **curframe )
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	int		i, j, numframes, frame;
	float		lerpFrac, time, jtime, jinterval;
	float		*pintervals, fullinterval, targettime;
	int		m_fDoInterp;

	psprite = ent->model->cache.data;
	frame = (int)ent->curstate.frame;
	lerpFrac = 1.0f;

	// misc info
	m_fDoInterp = (ent->curstate.effects & EF_NOINTERP) ? false : true;

	if( frame < 0 )
	{
		frame = 0;
	}
	else if( frame >= psprite->numframes )
	{
		gEngfuncs.Con_Reportf( S_WARN "%s: no such frame %d (%s)\n", __func__, frame, ent->model->name );
		frame = psprite->numframes - 1;
	}

	if( psprite->frames[frame].type == FRAME_SINGLE )
	{
		if( m_fDoInterp )
		{
			if( ent->latched.prevblending[0] >= psprite->numframes || psprite->frames[ent->latched.prevblending[0]].type != FRAME_SINGLE )
			{
				// this can be happens when rendering switched between single and angled frames
				// or change model on replace delta-entity
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = gp_cl->time;
				lerpFrac = 1.0f;
			}

			if( ent->latched.sequencetime < gp_cl->time )
			{
				if( frame != ent->latched.prevblending[1] )
				{
					ent->latched.prevblending[0] = ent->latched.prevblending[1];
					ent->latched.prevblending[1] = frame;
					ent->latched.sequencetime = gp_cl->time;
					lerpFrac = 0.0f;
				}
				else lerpFrac = (gp_cl->time - ent->latched.sequencetime) * 11.0f;
			}
			else
			{
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = gp_cl->time;
				lerpFrac = 0.0f;
			}
		}
		else
		{
			ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
			lerpFrac = 1.0f;
		}

		if( ent->latched.prevblending[0] >= psprite->numframes )
		{
			// reset interpolation on change model
			ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
			ent->latched.sequencetime = gp_cl->time;
			lerpFrac = 0.0f;
		}

		// get the interpolated frames
		if( oldframe ) *oldframe = psprite->frames[ent->latched.prevblending[0]].frameptr;
		if( curframe ) *curframe = psprite->frames[frame].frameptr;
	}
	else if( psprite->frames[frame].type == FRAME_GROUP )
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];
		jinterval = pintervals[1] - pintervals[0];
		time = gp_cl->time;
		jtime = 0.0f;

		// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
		// are positive, so we don't have to worry about division by zero
		targettime = time - ((int)(time / fullinterval)) * fullinterval;

		// LordHavoc: since I can't measure the time properly when it loops from numframes - 1 to 0,
		// i instead measure the time of the first frame, hoping it is consistent
		for( i = 0, j = numframes - 1; i < (numframes - 1); i++ )
		{
			if( pintervals[i] > targettime )
				break;
			j = i;
			jinterval = pintervals[i] - jtime;
			jtime = pintervals[i];
		}

		if( m_fDoInterp )
			lerpFrac = (targettime - jtime) / jinterval;
		else j = i; // no lerping

		// get the interpolated frames
		if( oldframe ) *oldframe = pspritegroup->frames[j];
		if( curframe ) *curframe = pspritegroup->frames[i];
	}
	else if( psprite->frames[frame].type == FRAME_ANGLED )
	{
		// e.g. doom-style sprite monsters
		float	yaw = ent->angles[YAW];
		int	angleframe = (int)(Q_rint(( RI.viewangles[1] - yaw + 45.0f ) / 360 * 8) - 4) & 7;

		if( m_fDoInterp )
		{
			if( ent->latched.prevblending[0] >= psprite->numframes || psprite->frames[ent->latched.prevblending[0]].type != FRAME_ANGLED )
			{
				// this can be happens when rendering switched between single and angled frames
				// or change model on replace delta-entity
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = gp_cl->time;
				lerpFrac = 1.0f;
			}

			if( ent->latched.sequencetime < gp_cl->time )
			{
				if( frame != ent->latched.prevblending[1] )
				{
					ent->latched.prevblending[0] = ent->latched.prevblending[1];
					ent->latched.prevblending[1] = frame;
					ent->latched.sequencetime = gp_cl->time;
					lerpFrac = 0.0f;
				}
				else lerpFrac = (gp_cl->time - ent->latched.sequencetime) * ent->curstate.framerate;
			}
			else
			{
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = gp_cl->time;
				lerpFrac = 0.0f;
			}
		}
		else
		{
			ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
			lerpFrac = 1.0f;
		}

		pspritegroup = (mspritegroup_t *)psprite->frames[ent->latched.prevblending[0]].frameptr;
		if( oldframe ) *oldframe = pspritegroup->frames[angleframe];

		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		if( curframe ) *curframe = pspritegroup->frames[angleframe];
	}

	return lerpFrac;
}

/*
================
R_CullSpriteModel

Cull sprite model by bbox
================
*/
static qboolean R_CullSpriteModel( cl_entity_t *e, vec3_t origin )
{
	vec3_t	sprite_mins, sprite_maxs;
	float	scale = 1.0f;

	if( !e->model->cache.data )
		return true;

	if( e->curstate.scale > 0.0f )
		scale = e->curstate.scale;

	// scale original bbox (no rotation for sprites)
	VectorScale( e->model->mins, scale, sprite_mins );
	VectorScale( e->model->maxs, scale, sprite_maxs );

	sprite_radius = RadiusFromBounds( sprite_mins, sprite_maxs );

	VectorAdd( sprite_mins, origin, sprite_mins );
	VectorAdd( sprite_maxs, origin, sprite_maxs );

	return R_CullModel( e, sprite_mins, sprite_maxs );
}

/*
================
R_GlowSightDistance

Set sprite brightness factor
================
*/
static float R_SpriteGlowBlend( vec3_t origin, int rendermode, int renderfx, float *pscale )
{
	float	dist, brightness;
	vec3_t	glowDist;
	pmtrace_t	*tr;

	VectorSubtract( origin, RI.vieworg, glowDist );
	dist = VectorLength( glowDist );

	if( RP_NORMALPASS( ))
	{
		tr = gEngfuncs.EV_VisTraceLine( RI.vieworg, origin, r_traceglow.value ? PM_GLASS_IGNORE : (PM_GLASS_IGNORE|PM_STUDIO_IGNORE));

		if(( 1.0f - tr->fraction ) * dist > 8.0f )
			return 0.0f;
	}

	if( renderfx == kRenderFxNoDissipation )
		return 1.0f;

	brightness = GLARE_FALLOFF / ( dist * dist );
	brightness = bound( 0.05f, brightness, 1.0f );
	*pscale *= dist * ( 1.0f / 200.0f );

	return brightness;
}

/*
================
R_SpriteOccluded

Do occlusion test for glow-sprites
================
*/
static qboolean R_SpriteOccluded( cl_entity_t *e, vec3_t origin, float *pscale )
{
	if( e->curstate.rendermode == kRenderGlow )
	{
		float	blend;
		vec3_t	v;

		TriWorldToScreen( origin, v );

		if( v[0] < RI.viewport[0] || v[0] > RI.viewport[0] + RI.viewport[2] )
			return true; // do scissor
		if( v[1] < RI.viewport[1] || v[1] > RI.viewport[1] + RI.viewport[3] )
			return true; // do scissor

		blend = R_SpriteGlowBlend( origin, e->curstate.rendermode, e->curstate.renderfx, pscale );
		tr.blend *= blend;

		if( blend <= 0.01f )
			return true; // faded
	}
	else
	{
		if( R_CullSpriteModel( e, origin ))
			return true;
	}

	return false;
}

/*
=================
R_DrawSpriteQuad
=================
*/
static void R_DrawSpriteQuad( mspriteframe_t *frame, vec3_t org, vec3_t v_right, vec3_t v_up, float scale, uint32_t argb_color, pvr_dr_state_t *dr_state )
{
	vec3_t	points[4];
	shz_vec4_t transformed[4];
	float	uv[4][2];
	unsigned vismask = 0;

	r_stats.c_sprite_polys++;

	// Build 4 corners of sprite quad
	// Bottom-left: org + down*v_up + left*v_right
	VectorMA( org, frame->down * scale, v_up, points[0] );
	VectorMA( points[0], frame->left * scale, v_right, points[0] );
	uv[0][0] = 0.0f;
	uv[0][1] = 1.0f;

	// Top-left: org + up*v_up + left*v_right
	VectorMA( org, frame->up * scale, v_up, points[1] );
	VectorMA( points[1], frame->left * scale, v_right, points[1] );
	uv[1][0] = 0.0f;
	uv[1][1] = 0.0f;

	// Top-right: org + up*v_up + right*v_right
	VectorMA( org, frame->up * scale, v_up, points[2] );
	VectorMA( points[2], frame->right * scale, v_right, points[2] );
	uv[2][0] = 1.0f;
	uv[2][1] = 0.0f;

	// Bottom-right: org + down*v_up + right*v_right
	VectorMA( org, frame->down * scale, v_up, points[3] );
	VectorMA( points[3], frame->right * scale, v_right, points[3] );
	uv[3][0] = 1.0f;
	uv[3][1] = 1.0f;

	// Transform all vertices using current world matrix (sh4zam requires 8-byte alignment)
	__attribute__((aligned(8))) float aligned_matrix[16];
	memcpy( aligned_matrix, r_world_matrix, sizeof( aligned_matrix ));
	shz_xmtrx_load_4x4((shz_mat4x4_t*)aligned_matrix);
	for( int i = 0; i < 4; i++ )
	{
		transformed[i] = shz_xmtrx_transform_vec4(shz_vec3_vec4(shz_vec3_init(points[i][0], points[i][1], points[i][2]), 1.0f));
		if( transformed[i].z >= -transformed[i].w )
			vismask |= (1U << i);
	}

	// Early out if entire quad is behind near plane
	if( vismask == 0 )
		return;

	// Submit as two triangles (fan: 0-1-2, 0-2-3)
	// Triangle 1: 0, 1, 2
	if( (vismask & 7) == 7 )
	{
		// All visible - fast path
		float inv_w0 = shz_invf_fsrra(transformed[0].w);
		float inv_w1 = shz_invf_fsrra(transformed[1].w);
		float inv_w2 = shz_invf_fsrra(transformed[2].w);

		pvr_vertex_t *vert = pvr_dr_target(*dr_state);
		vert->flags = PVR_CMD_VERTEX;
		vert->x = transformed[0].x * inv_w0;
		vert->y = transformed[0].y * inv_w0;
		vert->z = inv_w0;
		vert->u = uv[0][0];
		vert->v = uv[0][1];
		vert->argb = argb_color;
		vert->oargb = 0;
		pvr_dr_commit(vert);

		vert = pvr_dr_target(*dr_state);
		vert->flags = PVR_CMD_VERTEX;
		vert->x = transformed[1].x * inv_w1;
		vert->y = transformed[1].y * inv_w1;
		vert->z = inv_w1;
		vert->u = uv[1][0];
		vert->v = uv[1][1];
		vert->argb = argb_color;
		vert->oargb = 0;
		pvr_dr_commit(vert);

		vert = pvr_dr_target(*dr_state);
		vert->flags = PVR_CMD_VERTEX;
		vert->x = transformed[2].x * inv_w2;
		vert->y = transformed[2].y * inv_w2;
		vert->z = inv_w2;
		vert->u = uv[2][0];
		vert->v = uv[2][1];
		vert->argb = argb_color;
		vert->oargb = 0;
		pvr_dr_commit(vert);
	}
	else
	{
		// Clip triangle against near plane
		PVR_ClipAndSubmitTriangle(
			dr_state,
			transformed[0], transformed[1], transformed[2],
			uv[0][0], uv[0][1],
			uv[1][0], uv[1][1],
			uv[2][0], uv[2][1],
			argb_color, argb_color, argb_color
		);
	}

	// Triangle 2: 0, 2, 3
	if( ((vismask & 5) == 5) && (vismask & 2) ) // 0,2,3 visible (or 0,2 visible and 3 might be clipped)
	{
		if( (vismask & 13) == 13 ) // All visible (0,2,3)
		{
			float inv_w0 = shz_invf_fsrra(transformed[0].w);
			float inv_w2 = shz_invf_fsrra(transformed[2].w);
			float inv_w3 = shz_invf_fsrra(transformed[3].w);

			pvr_vertex_t *vert = pvr_dr_target(*dr_state);
			vert->flags = PVR_CMD_VERTEX;
			vert->x = transformed[0].x * inv_w0;
			vert->y = transformed[0].y * inv_w0;
			vert->z = inv_w0;
			vert->u = uv[0][0];
			vert->v = uv[0][1];
			vert->argb = argb_color;
			vert->oargb = 0;
			pvr_dr_commit(vert);

			vert = pvr_dr_target(*dr_state);
			vert->flags = PVR_CMD_VERTEX;
			vert->x = transformed[2].x * inv_w2;
			vert->y = transformed[2].y * inv_w2;
			vert->z = inv_w2;
			vert->u = uv[2][0];
			vert->v = uv[2][1];
			vert->argb = argb_color;
			vert->oargb = 0;
			pvr_dr_commit(vert);

			vert = pvr_dr_target(*dr_state);
			vert->flags = PVR_CMD_VERTEX_EOL;
			vert->x = transformed[3].x * inv_w3;
			vert->y = transformed[3].y * inv_w3;
			vert->z = inv_w3;
			vert->u = uv[3][0];
			vert->v = uv[3][1];
			vert->argb = argb_color;
			vert->oargb = 0;
			pvr_dr_commit(vert);
		}
		else
		{
			// Clip triangle against near plane
			PVR_ClipAndSubmitTriangle(
				dr_state,
				transformed[0], transformed[2], transformed[3],
				uv[0][0], uv[0][1],
				uv[2][0], uv[2][1],
				uv[3][0], uv[3][1],
				argb_color, argb_color, argb_color
			);
		}
	}
}

/*
=================
R_SpriteQuadAnyVisible

Quick near-plane visibility test to avoid submitting a polygon header with no vertices.
If a header is committed and all quads early-out, the TA stream can be corrupted on real HW.
=================
*/
static qboolean R_SpriteQuadAnyVisible( mspriteframe_t *frame, vec3_t org, vec3_t v_right, vec3_t v_up, float scale )
{
	vec3_t	points[4];
	unsigned vismask = 0;

	// Build corners (same as R_DrawSpriteQuad)
	VectorMA( org, frame->down * scale, v_up, points[0] );
	VectorMA( points[0], frame->left * scale, v_right, points[0] );

	VectorMA( org, frame->up * scale, v_up, points[1] );
	VectorMA( points[1], frame->left * scale, v_right, points[1] );

	VectorMA( org, frame->up * scale, v_up, points[2] );
	VectorMA( points[2], frame->right * scale, v_right, points[2] );

	VectorMA( org, frame->down * scale, v_up, points[3] );
	VectorMA( points[3], frame->right * scale, v_right, points[3] );

	__attribute__((aligned(8))) float aligned_matrix[16];
	memcpy( aligned_matrix, r_world_matrix, sizeof( aligned_matrix ));
	shz_xmtrx_load_4x4((shz_mat4x4_t*)aligned_matrix);

	for( int i = 0; i < 4; i++ )
	{
		shz_vec4_t tp = shz_xmtrx_transform_vec4( shz_vec3_vec4( shz_vec3_init( points[i][0], points[i][1], points[i][2] ), 1.0f ));
		if( tp.z >= -tp.w )
			vismask |= (1U << i);
	}

	return ( vismask != 0 ) ? true : false;
}

static qboolean R_SpriteHasLightmap( cl_entity_t *e, int texFormat )
{
	if( !r_sprite_lighting->value )
		return false;

	if( texFormat != SPR_ALPHTEST )
		return false;

	if( FBitSet( e->curstate.effects, EF_FULLBRIGHT ))
		return false;

	if( e->curstate.renderamt <= 127 )
		return false;

	switch( e->curstate.rendermode )
	{
	case kRenderNormal:
	case kRenderTransAlpha:
	case kRenderTransTexture:
		break;
	default:
		return false;
	}

	return true;
}

/*
=================
R_SpriteAllowLerping
=================
*/
static qboolean R_SpriteAllowLerping( cl_entity_t *e, msprite_t *psprite )
{
	if( !r_sprite_lerping->value )
		return false;

	if( psprite->numframes <= 1 )
		return false;

	if( psprite->texFormat != SPR_ADDITIVE )
		return false;

	if( e->curstate.rendermode == kRenderNormal || e->curstate.rendermode == kRenderTransAlpha )
		return false;

	return true;
}

/*
=================
R_DrawSpriteModel
=================
*/
void R_DrawSpriteModel( cl_entity_t *e )
{

	mspriteframe_t	*frame, *oldframe;
	msprite_t		*psprite;
	model_t		*model;
	int		i, type;
	float		angle, dot, sr, cr;
	float		lerp = 1.0f, ilerp, scale;
	vec3_t		v_forward, v_right, v_up;
	vec3_t		origin, color, color2 = { 0.0f };

	if( RI.params & RP_ENVVIEW )
		return;

	model = e->model;
	psprite = (msprite_t * )model->cache.data;
	VectorCopy( e->origin, origin );	// set render origin

	// do movewith
	if( e->curstate.aiment > 0 && e->curstate.movetype == MOVETYPE_FOLLOW )
	{
		cl_entity_t	*parent;

		parent = CL_GetEntityByIndex( e->curstate.aiment );

		if( parent && parent->model )
		{
			if( parent->model->type == mod_studio && e->curstate.body > 0 )
			{
				int num = bound( 1, e->curstate.body, MAXSTUDIOATTACHMENTS );
				VectorCopy( parent->attachment[num-1], origin );
			}
			else VectorCopy( parent->origin, origin );
		}
	}

	scale = e->curstate.scale;
	if( !scale ) scale = 1.0f;

	if( R_SpriteOccluded( e, origin, &scale ))
		return; // sprite culled

	r_stats.c_sprite_models_drawn++;

	// Determine sprite color (never pass '0 0 0' - Valve Hammer Editor bug)
	if( e->curstate.rendercolor.r || e->curstate.rendercolor.g || e->curstate.rendercolor.b )
	{
		color[0] = (float)e->curstate.rendercolor.r * ( 1.0f / 255.0f );
		color[1] = (float)e->curstate.rendercolor.g * ( 1.0f / 255.0f );
		color[2] = (float)e->curstate.rendercolor.b * ( 1.0f / 255.0f );
	}
	else
	{
		color[0] = 1.0f;
		color[1] = 1.0f;
		color[2] = 1.0f;
	}

	// Get lightmap color if applicable
	if( R_SpriteHasLightmap( e, psprite->texFormat ))
	{
		colorVec lightColor = R_LightPoint( origin );
		color2[0] = (float)lightColor.r * ( 1.0f / 255.0f );
		color2[1] = (float)lightColor.g * ( 1.0f / 255.0f );
		color2[2] = (float)lightColor.b * ( 1.0f / 255.0f );
	}

	// Get sprite frames (with lerping if enabled)
	if( R_SpriteAllowLerping( e, psprite ))
		lerp = R_GetSpriteFrameInterpolant( e, &oldframe, &frame );
	else frame = oldframe = R_GetSpriteFrame( model, e->curstate.frame, e->angles[YAW] );

	type = psprite->type;

	// automatically roll parallel sprites if requested
	if( e->angles[ROLL] != 0.0f && type == SPR_FWD_PARALLEL )
		type = SPR_FWD_PARALLEL_ORIENTED;

	// Calculate sprite orientation vectors
	switch( type )
	{
	case SPR_ORIENTED:
		AngleVectors( e->angles, v_forward, v_right, v_up );
		VectorScale( v_forward, 0.01f, v_forward );	// to avoid z-fighting
		VectorSubtract( origin, v_forward, origin );
		break;
	case SPR_FACING_UPRIGHT:
		VectorSet( v_right, origin[1] - RI.vieworg[1], -(origin[0] - RI.vieworg[0]), 0.0f );
		VectorSet( v_up, 0.0f, 0.0f, 1.0f );
		VectorNormalize( v_right );
		break;
	case SPR_FWD_PARALLEL_UPRIGHT:
		dot = RI.vforward[2];
		if(( dot > 0.999848f ) || ( dot < -0.999848f ))	// cos(1 degree) = 0.999848
			return; // invisible
		VectorSet( v_up, 0.0f, 0.0f, 1.0f );
		VectorSet( v_right, RI.vforward[1], -RI.vforward[0], 0.0f );
		VectorNormalize( v_right );
		break;
	case SPR_FWD_PARALLEL_ORIENTED:
		angle = e->angles[ROLL] * (M_PI2 / 360.0f);
		SinCos( angle, &sr, &cr );
		for( i = 0; i < 3; i++ )
		{
			v_right[i] = (RI.vright[i] * cr + RI.vup[i] * sr);
			v_up[i] = RI.vright[i] * -sr + RI.vup[i] * cr;
		}
		break;
	case SPR_FWD_PARALLEL: // normal sprite
	default:
		VectorCopy( RI.vright, v_right );
		VectorCopy( RI.vup, v_up );
		break;
	}

	// Use the currently open PVR list (sprites are called from TR list in R_DrawEntitiesOnList)
	int list = PVR_LIST_TR_POLY;
	int rendermode = e->curstate.rendermode;
	float blend = 1.0f;

	// Calculate sprite blend locally (do NOT rely on global tr.blend which may be stale here).
	if( rendermode != kRenderNormal )
		blend = CL_FxBlend( e ) / 255.0f;
	if( blend <= 0.0f )
	{
		if( e->curstate.rendermode == kRenderGlow || e->curstate.rendermode == kRenderTransAdd )
			R_AllowFog( true );
		return;
	}

	// Sprite vertices are built in world space already (origin + v_right/v_up),
	// so keep the world matrix. Applying entity transform here would double-translate.
	R_LoadIdentity();

	// Initialize PVR direct rendering
	pvr_dr_state_t dr_state;
	pvr_dr_init(&dr_state);
	pvr_poly_hdr_t *hdr; 

	// Get texture for current frame
	int texnum = frame->gl_texturenum;
	gl_texture_t *glt = R_GetTexture( texnum );
	if( !glt || !glt->loaded || !glt->vram_ptr )
		glt = R_GetTexture( tr.defaultTexture );

	if( !glt || !glt->loaded || !glt->vram_ptr )
	{
		pvr_dr_finish();
		if( e->curstate.rendermode == kRenderGlow || e->curstate.rendermode == kRenderTransAdd )
			R_AllowFog( true );
		return;
	}

	// Setup polygon context for sprite
	pvr_poly_cxt_t cxt;
	pvr_poly_cxt_txr(&cxt, list, glt->format, glt->width, glt->height, glt->vram_ptr, PVR_FILTER_BILINEAR);
	cxt.gen.culling = (psprite->facecull == SPR_CULL_NONE) ? PVR_CULLING_NONE : PVR_CULLING_CW;
	// Flat shading is default, don't set explicitly
	cxt.gen.fog_type = glState.isFogEnabled ? PVR_FOG_TABLE : PVR_FOG_DISABLE;
	// Need MODULATEALPHA so vertex alpha (blend) affects output alpha (like GL_MODULATE).
	cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
	// Critical for correct sprite transparency + avoid distance black-quads (mip sampling / wrap).
	cxt.txr.alpha = PVR_TXRALPHA_ENABLE;
	cxt.txr.uv_flip = PVR_UVFLIP_NONE;
	cxt.txr.uv_clamp = PVR_UVCLAMP_UV;
	cxt.txr.mipmap = PVR_MIPMAP_DISABLE;
	cxt.txr.mipmap_bias = PVR_MIPBIAS_NORMAL;

	// Sprites: always alpha-enabled + blended so texture alpha can punch through.
	cxt.gen.alpha = PVR_ALPHA_ENABLE;
	cxt.depth.comparison = (rendermode == kRenderGlow) ? PVR_DEPTHCMP_ALWAYS : PVR_DEPTHCMP_GEQUAL;
	cxt.depth.write = PVR_DEPTHWRITE_DISABLE;

	switch( rendermode )
	{
	case kRenderTransAdd:
	case kRenderGlow:
		// GL: glBlendFunc(GL_SRC_ALPHA, GL_ONE)
		cxt.blend.src = PVR_BLEND_SRCALPHA;
		cxt.blend.dst = PVR_BLEND_ONE;
		break;
	default:
		cxt.blend.src = PVR_BLEND_SRCALPHA;
		cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
		break;
	}
	// Don't explicitly enable blending - it's enabled automatically when alpha is enabled and blend src/dst are set

	// Pack vertex color (alpha comes from blend)
	uint32_t argb_base = PVR_PACK_COLOR(blend, color[0], color[1], color[2]);

	// Draw sprite frame(s)
	if( oldframe == frame )
	{
		// Real HW safety: don't submit header if quad is fully behind near plane.
		if( !R_SpriteQuadAnyVisible( frame, origin, v_right, v_up, scale ))
		{
			pvr_dr_finish();
			if( e->curstate.rendermode == kRenderGlow || e->curstate.rendermode == kRenderTransAdd )
				R_AllowFog( true );
			return;
		}

		// Compile and submit header
		hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
		pvr_poly_compile(hdr, &cxt);
		pvr_dr_commit(hdr);

		// Single non-lerped frame
		R_DrawSpriteQuad( frame, origin, v_right, v_up, scale, argb_base, &dr_state );
	}
	else
	{
		// Lerped frames: draw both with alpha blending
		lerp = bound( 0.0f, lerp, 1.0f );
		ilerp = 1.0f - lerp;

		// Real HW safety: don't submit header if neither quad would emit vertices.
		// (Geometry can differ between frames: different extents in the sprite frames)
		qboolean any_visible = false;
		if( ilerp > 0.0f )
			any_visible |= R_SpriteQuadAnyVisible( oldframe, origin, v_right, v_up, scale );
		if( lerp > 0.0f )
			any_visible |= R_SpriteQuadAnyVisible( frame, origin, v_right, v_up, scale );
		if( !any_visible )
		{
			pvr_dr_finish();
			if( e->curstate.rendermode == kRenderGlow || e->curstate.rendermode == kRenderTransAdd )
				R_AllowFog( true );
			return;
		}

		// Compile and submit header for the base frame (we may recompile below for old/new textures).
		hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
		pvr_poly_compile(hdr, &cxt);
		pvr_dr_commit(hdr);

		if( ilerp > 0.0f )
		{
			uint32_t argb_old = PVR_PACK_COLOR(blend * ilerp, color[0], color[1], color[2]);
			gl_texture_t *glt_old = R_GetTexture( oldframe->gl_texturenum );
			if( glt_old && glt_old->loaded && glt_old->vram_ptr )
			{
				// Recompile header for old frame texture
				pvr_poly_cxt_txr(&cxt, list, glt_old->format, glt_old->width, glt_old->height, glt_old->vram_ptr, PVR_FILTER_BILINEAR);
				cxt.gen.culling = (psprite->facecull == SPR_CULL_NONE) ? PVR_CULLING_NONE : PVR_CULLING_CW;
				// Flat shading is default, don't set explicitly
				cxt.gen.fog_type = glState.isFogEnabled ? PVR_FOG_TABLE : PVR_FOG_DISABLE;
				cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
				cxt.txr.alpha = PVR_TXRALPHA_ENABLE;
				cxt.txr.uv_flip = PVR_UVFLIP_NONE;
				cxt.txr.uv_clamp = PVR_UVCLAMP_UV;
				cxt.txr.mipmap = PVR_MIPMAP_DISABLE;
				cxt.txr.mipmap_bias = PVR_MIPBIAS_NORMAL;
				cxt.gen.alpha = PVR_ALPHA_ENABLE;
				cxt.depth.comparison = (rendermode == kRenderGlow) ? PVR_DEPTHCMP_ALWAYS : PVR_DEPTHCMP_GEQUAL;
				cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
				if( rendermode == kRenderTransAdd || rendermode == kRenderGlow )
				{
					cxt.blend.src = PVR_BLEND_SRCALPHA;
					cxt.blend.dst = PVR_BLEND_ONE;
				}
				else
				{
					cxt.blend.src = PVR_BLEND_SRCALPHA;
					cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
				}
				// Don't explicitly enable blending - it's enabled automatically when alpha is enabled and blend src/dst are set
				hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
				pvr_poly_compile(hdr, &cxt);
				pvr_dr_commit(hdr);
			}
			R_DrawSpriteQuad( oldframe, origin, v_right, v_up, scale, argb_old, &dr_state );
		}

		if( lerp > 0.0f )
		{
			uint32_t argb_new = PVR_PACK_COLOR(blend * lerp, color[0], color[1], color[2]);
			// Recompile header for new frame texture (if different)
			if( frame->gl_texturenum != oldframe->gl_texturenum )
			{
				gl_texture_t *glt_new = R_GetTexture( frame->gl_texturenum );
				if( glt_new && glt_new->loaded && glt_new->vram_ptr )
				{
					pvr_poly_cxt_txr(&cxt, list, glt_new->format, glt_new->width, glt_new->height, glt_new->vram_ptr, PVR_FILTER_BILINEAR);
					cxt.gen.culling = (psprite->facecull == SPR_CULL_NONE) ? PVR_CULLING_NONE : PVR_CULLING_CW;
					// Flat shading is default, don't set explicitly
					cxt.gen.fog_type = glState.isFogEnabled ? PVR_FOG_TABLE : PVR_FOG_DISABLE;
					cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
					cxt.txr.alpha = PVR_TXRALPHA_ENABLE;
					cxt.txr.uv_flip = PVR_UVFLIP_NONE;
					cxt.txr.uv_clamp = PVR_UVCLAMP_UV;
					cxt.txr.mipmap = PVR_MIPMAP_DISABLE;
					cxt.txr.mipmap_bias = PVR_MIPBIAS_NORMAL;
					cxt.gen.alpha = PVR_ALPHA_ENABLE;
					cxt.depth.comparison = (rendermode == kRenderGlow) ? PVR_DEPTHCMP_ALWAYS : PVR_DEPTHCMP_GEQUAL;
					cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
					if( rendermode == kRenderTransAdd || rendermode == kRenderGlow )
					{
						cxt.blend.src = PVR_BLEND_SRCALPHA;
						cxt.blend.dst = PVR_BLEND_ONE;
					}
					else
					{
						cxt.blend.src = PVR_BLEND_SRCALPHA;
						cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
					}
					// Don't explicitly enable blending - it's enabled automatically when alpha is enabled and blend src/dst are set
					hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
					pvr_poly_compile(hdr, &cxt);
					pvr_dr_commit(hdr);
				}
			}
			R_DrawSpriteQuad( frame, origin, v_right, v_up, scale, argb_new, &dr_state );
		}
	}

	// Draw sprite lightmap (multiply blend pass) if applicable
	if( R_SpriteHasLightmap( e, psprite->texFormat ) && !r_lightmap->value )
	{
		// Lightmap pass: use white texture, multiply blend, depth=EQUAL
		gl_texture_t *glt_white = R_GetTexture( tr.whiteTexture );
		if( glt_white && glt_white->loaded && glt_white->vram_ptr )
		{
			pvr_poly_cxt_t cxt_lm;
			pvr_poly_cxt_txr(&cxt_lm, PVR_LIST_TR_POLY, glt_white->format, glt_white->width, glt_white->height, glt_white->vram_ptr, PVR_FILTER_BILINEAR);
			cxt_lm.gen.culling = (psprite->facecull == SPR_CULL_NONE) ? PVR_CULLING_NONE : PVR_CULLING_CW;
			// Flat shading is default, don't set explicitly
			cxt_lm.gen.fog_type = PVR_FOG_DISABLE; // no fog on lightmap pass
			cxt_lm.txr.env = PVR_TXRENV_MODULATEALPHA;
			cxt_lm.txr.alpha = PVR_TXRALPHA_ENABLE;
			cxt_lm.txr.uv_flip = PVR_UVFLIP_NONE;
			cxt_lm.txr.uv_clamp = PVR_UVCLAMP_UV;
			cxt_lm.txr.mipmap = PVR_MIPMAP_DISABLE;
			cxt_lm.txr.mipmap_bias = PVR_MIPBIAS_NORMAL;
			cxt_lm.gen.alpha = PVR_ALPHA_ENABLE;
			cxt_lm.depth.comparison = PVR_DEPTHCMP_EQUAL; // only where sprite was drawn
			cxt_lm.depth.write = PVR_DEPTHWRITE_DISABLE;
			cxt_lm.blend.src = PVR_BLEND_DESTCOLOR;
			cxt_lm.blend.dst = PVR_BLEND_ZERO;
			// Don't explicitly enable blending - it's enabled automatically when alpha is enabled and blend src/dst are set

			hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
			pvr_poly_compile(hdr, &cxt_lm);
			pvr_dr_commit(hdr);

			uint32_t argb_lm = PVR_PACK_COLOR(blend, color2[0], color2[1], color2[2]);
			R_DrawSpriteQuad( frame, origin, v_right, v_up, scale, argb_lm, &dr_state );
		}
	}

	pvr_dr_finish();

	// Restore fog if disabled
	if( e->curstate.rendermode == kRenderGlow || e->curstate.rendermode == kRenderTransAdd )
		R_AllowFog( true );
}
