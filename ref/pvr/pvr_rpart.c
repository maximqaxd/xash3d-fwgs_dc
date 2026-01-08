/*
pvr_part.c - particles and tracers
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
#include "r_efx.h"
#include "event_flags.h"
#include "entity_types.h"
#include "triangleapi.h"
#include "pm_local.h"
#include "studio.h"

static float gTracerSize[11] = { 1.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static color24 gTracerColors[] =
{
{ 255, 255, 255 },		// White
{ 255, 0, 0 },		// Red
{ 0, 255, 0 },		// Green
{ 0, 0, 255 },		// Blue
{ 0, 0, 0 },		// Tracer default, filled in from cvars, etc.
{ 255, 167, 17 },		// Yellow-orange sparks
{ 255, 130, 90 },		// Yellowish streaks (garg)
{ 55, 60, 144 },		// Blue egon streak
{ 255, 130, 90 },		// More Yellowish streaks (garg)
{ 255, 140, 90 },		// More Yellowish streaks (garg)
{ 200, 130, 90 },		// More red streaks (garg)
{ 255, 120, 70 },		// Darker red streaks (garg)
};

/*
================
CL_DrawParticles

update particle color, position, free expired and draw it
================
*/
void CL_DrawParticles( double frametime, particle_t *cl_active_particles, float partsize )
{
	particle_t	*p;
	vec3_t		right, up;
	color24		color;
	int		alpha;
	float		size;

	if( !cl_active_particles )
		return;	// nothing to draw?

	// Particles are translucent, render in TR list
	// We assume the caller is inside PVR_LIST_TR_POLY (see pvr_rmain.c or CL_DrawEFX)
	pvr_dr_state_t dr_state;
	pvr_dr_init( &dr_state );

	// Get particle texture
	gl_texture_t *glt = R_GetTexture( tr.particleTexture );
	if( !glt || !glt->loaded || !glt->vram_ptr )
	{
		pvr_dr_finish();
		return;
	}

	// Setup PVR context for translucent particles
	pvr_poly_cxt_t cxt;
	pvr_poly_cxt_txr( &cxt, PVR_LIST_TR_POLY, glt->format, glt->width, glt->height, glt->vram_ptr, PVR_FILTER_BILINEAR );
	cxt.gen.culling = PVR_CULLING_NONE;
	cxt.gen.fog_type = glState.isFogEnabled ? PVR_FOG_TABLE : PVR_FOG_DISABLE;
	cxt.gen.alpha = PVR_ALPHA_ENABLE;
	cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
	cxt.txr.alpha = PVR_TXRALPHA_ENABLE;
	cxt.txr.uv_flip = PVR_UVFLIP_NONE;
	cxt.txr.uv_clamp = PVR_UVCLAMP_UV;
	cxt.txr.mipmap = PVR_MIPMAP_DISABLE;
	cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
	cxt.depth.write = PVR_DEPTHWRITE_DISABLE; // No depth write for particles
	cxt.blend.src = PVR_BLEND_SRCALPHA;
	cxt.blend.dst = PVR_BLEND_INVSRCALPHA;

	// Submit header once
	pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target( dr_state );
	pvr_poly_compile( hdr, &cxt );
	pvr_dr_commit( hdr );

	// Load world matrix (should be viewproj after R_LoadIdentity())
	__attribute__((aligned(8))) float aligned_matrix[16];
	memcpy( aligned_matrix, r_world_matrix, sizeof( aligned_matrix ));
	shz_xmtrx_load_4x4((shz_mat4x4_t*)aligned_matrix);

	for( p = cl_active_particles; p; p = p->next )
	{
		if(( p->type != pt_blob ) || ( p->packedColor == 255 ))
		{
			size = partsize; // get initial size of particle

			// scale up to keep particles from disappearing
			size += (p->org[0] - RI.vieworg[0]) * RI.cull_vforward[0];
			size += (p->org[1] - RI.vieworg[1]) * RI.cull_vforward[1];
			size += (p->org[2] - RI.vieworg[2]) * RI.cull_vforward[2];

			if( size < 20.0f ) size = partsize;
			else size = partsize + size * 0.002f;

			// scale the axes by radius
			VectorScale( RI.cull_vright, size, right );
			VectorScale( RI.cull_vup, size, up );

			p->color = bound( 0, p->color, 255 );
			color = tr.palette[p->color];

			alpha = 255 * (p->die - gp_cl->time) * 16.0f;
			if( alpha > 255 || p->type == pt_static )
				alpha = 255;

			// Pack color as ARGB
			uint32_t argb = (alpha << 24) | (color.r << 16) | (color.g << 8) | color.b;

			// Build 4 corners of particle quad
			vec3_t corners[4];
			VectorAdd( p->org, right, corners[0] );
			VectorSubtract( corners[0], up, corners[0] ); // org + right - up
			VectorAdd( p->org, right, corners[1] );
			VectorAdd( corners[1], up, corners[1] ); // org + right + up
			VectorSubtract( p->org, right, corners[2] );
			VectorAdd( corners[2], up, corners[2] ); // org - right + up
			VectorSubtract( p->org, right, corners[3] );
			VectorSubtract( corners[3], up, corners[3] ); // org - right - up

			// Transform vertices
			shz_vec4_t transformed[4];
			for( int i = 0; i < 4; i++ )
			{
				transformed[i] = shz_xmtrx_transform_vec4( shz_vec3_vec4( shz_vec3_init( corners[i][0], corners[i][1], corners[i][2] ), 1.0f ));
			}

			// UV coordinates (matching GL order)
			float uv[4][2] = {
				{ 0.0f, 1.0f }, // corner 0
				{ 0.0f, 0.0f }, // corner 1
				{ 1.0f, 0.0f }, // corner 2
				{ 1.0f, 1.0f }  // corner 3
			};

			// Submit as two triangles (fan: 0-1-2, 0-2-3)
			PVR_ClipAndSubmitTriangle( &dr_state,
				transformed[0], transformed[1], transformed[2],
				uv[0][0], uv[0][1],
				uv[1][0], uv[1][1],
				uv[2][0], uv[2][1],
				argb, argb, argb
			);
			PVR_ClipAndSubmitTriangle( &dr_state,
				transformed[0], transformed[2], transformed[3],
				uv[0][0], uv[0][1],
				uv[2][0], uv[2][1],
				uv[3][0], uv[3][1],
				argb, argb, argb
			);

			r_stats.c_particle_count++;
		}

		gEngfuncs.CL_ThinkParticle( frametime, p );
	}

	pvr_dr_finish();
}

/*
================
CL_CullTracer

check tracer bbox
================
*/
static qboolean CL_CullTracer( particle_t *p, const vec3_t start, const vec3_t end )
{
	vec3_t	mins, maxs;
	int	i;

	// compute the bounding box
	for( i = 0; i < 3; i++ )
	{
		if( start[i] < end[i] )
		{
			mins[i] = start[i];
			maxs[i] = end[i];
		}
		else
		{
			mins[i] = end[i];
			maxs[i] = start[i];
		}

		// don't let it be zero sized
		if( mins[i] == maxs[i] )
		{
			maxs[i] += gTracerSize[p->type] * 2.0f;
		}
	}

	// check bbox
	return R_CullBox( mins, maxs );
}

/*
================
CL_DrawTracers

update tracer color, position, free expired and draw it
================
*/
void CL_DrawTracers( double frametime, particle_t *cl_active_tracers )
{
	float		scale, atten, gravity;
	vec3_t		screenLast, screen;
	vec3_t		start, end, delta;
	particle_t	*p;

	// update tracer color if this is changed
	if( FBitSet( tracerred->flags|tracergreen->flags|tracerblue->flags|traceralpha->flags, FCVAR_CHANGED ))
	{
		color24 *customColors = &gTracerColors[4];
		customColors->r = (byte)(tracerred->value * traceralpha->value * 255);
		customColors->g = (byte)(tracergreen->value * traceralpha->value * 255);
		customColors->b = (byte)(tracerblue->value * traceralpha->value * 255);
		ClearBits( tracerred->flags, FCVAR_CHANGED );
		ClearBits( tracergreen->flags, FCVAR_CHANGED );
		ClearBits( tracerblue->flags, FCVAR_CHANGED );
		ClearBits( traceralpha->flags, FCVAR_CHANGED );
	}

	if( !cl_active_tracers )
		return;	// nothing to draw?

	if( !TriSpriteTexture( gEngfuncs.GetDefaultSprite( REF_DOT_SPRITE ), 0 ))
		return;

	// Tracers are translucent with additive blending, render in TR list
	// We assume the caller is inside PVR_LIST_TR_POLY (see pvr_rmain.c or CL_DrawEFX)
	pvr_dr_state_t dr_state;
	pvr_dr_init( &dr_state );

	// Get sprite texture (from TriSpriteTexture call above)
	int sprite_texnum = glState.currentTexturesIndex;
	gl_texture_t *glt = R_GetTexture( sprite_texnum );
	if( !glt || !glt->loaded || !glt->vram_ptr )
	{
		pvr_dr_finish();
		return;
	}

	// Setup PVR context for additive blending tracers
	pvr_poly_cxt_t cxt;
	pvr_poly_cxt_txr( &cxt, PVR_LIST_TR_POLY, glt->format, glt->width, glt->height, glt->vram_ptr, PVR_FILTER_BILINEAR );
	cxt.gen.culling = PVR_CULLING_NONE;
	cxt.gen.fog_type = glState.isFogEnabled ? PVR_FOG_TABLE : PVR_FOG_DISABLE;
	cxt.gen.alpha = PVR_ALPHA_ENABLE;
	cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
	cxt.txr.alpha = PVR_TXRALPHA_ENABLE;
	cxt.txr.uv_flip = PVR_UVFLIP_NONE;
	cxt.txr.uv_clamp = PVR_UVCLAMP_UV;
	cxt.txr.mipmap = PVR_MIPMAP_DISABLE;
	cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
	cxt.depth.write = PVR_DEPTHWRITE_DISABLE; // No depth write for tracers
	cxt.blend.src = PVR_BLEND_SRCALPHA;
	cxt.blend.dst = PVR_BLEND_ONE; // Additive blending

	// Submit header once
	pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target( dr_state );
	pvr_poly_compile( hdr, &cxt );
	pvr_dr_commit( hdr );

	// Load world matrix (should be viewproj after R_LoadIdentity())
	__attribute__((aligned(8))) float aligned_matrix[16];
	memcpy( aligned_matrix, r_world_matrix, sizeof( aligned_matrix ));
	shz_xmtrx_load_4x4((shz_mat4x4_t*)aligned_matrix);

	gravity = frametime * tr.movevars->gravity;
	scale = 1.0 - (frametime * 0.9);
	if( scale < 0.0f ) scale = 0.0f;

	for( p = cl_active_tracers; p; p = p->next )
	{
		atten = (p->die - gp_cl->time);
		if( atten > 0.1f ) atten = 0.1f;

		VectorScale( p->vel, ( p->ramp * atten ), delta );
		VectorAdd( p->org, delta, end );
		VectorCopy( p->org, start );

		if( !CL_CullTracer( p, start, end ))
		{
			vec3_t	verts[4], tmp2;
			vec3_t	tmp, normal;
			color24	color;

			// Transform point into screen space
			TriWorldToScreen( start, screen );
			TriWorldToScreen( end, screenLast );

			// build world-space normal to screen-space direction vector
			VectorSubtract( screen, screenLast, tmp );

			// we don't need Z, we're in screen space
			tmp[2] = 0;
			VectorNormalize( tmp );

			// build point along normal line (normal is -y, x)
			VectorScale( RI.cull_vup, tmp[0] * gTracerSize[p->type], normal );
			VectorScale( RI.cull_vright, -tmp[1] * gTracerSize[p->type], tmp2 );
			VectorSubtract( normal, tmp2, normal );

			// compute four vertexes
			VectorSubtract( start, normal, verts[0] );
			VectorAdd( start, normal, verts[1] );
			VectorAdd( verts[0], delta, verts[2] );
			VectorAdd( verts[1], delta, verts[3] );

			if( p->color < 0 || p->color >= sizeof( gTracerColors ) / sizeof( gTracerColors[0] ))
			{
				p->color = TRACER_COLORINDEX_DEFAULT;
			}

			color = gTracerColors[p->color];
			// Pack color as ARGB (alpha from packedColor)
			uint32_t argb = (p->packedColor << 24) | (color.r << 16) | (color.g << 8) | color.b;

			// Transform vertices
			shz_vec4_t transformed[4];
			for( int i = 0; i < 4; i++ )
			{
				transformed[i] = shz_xmtrx_transform_vec4( shz_vec3_vec4( shz_vec3_init( verts[i][0], verts[i][1], verts[i][2] ), 1.0f ));
			}

			// UV coordinates matching GL order: verts[2] (0.0, 0.8), verts[3] (1.0, 0.8), verts[1] (1.0, 0.0), verts[0] (0.0, 0.0)
			// GL submitted as: 2, 3, 1, 0 (which forms a valid quad when triangulated)
			// We'll submit as: 2-3-1 (first tri), 2-1-0 (second tri)
			float uv[4][2] = {
				{ 0.0f, 0.0f }, // verts[0]
				{ 1.0f, 0.0f }, // verts[1]
				{ 0.0f, 0.8f }, // verts[2]
				{ 1.0f, 0.8f }  // verts[3]
			};

			// Submit as two triangles matching GL order: 2-3-1, 2-1-0
			PVR_ClipAndSubmitTriangle( &dr_state,
				transformed[2], transformed[3], transformed[1],
				uv[2][0], uv[2][1],
				uv[3][0], uv[3][1],
				uv[1][0], uv[1][1],
				argb, argb, argb
			);
			PVR_ClipAndSubmitTriangle( &dr_state,
				transformed[2], transformed[1], transformed[0],
				uv[2][0], uv[2][1],
				uv[1][0], uv[1][1],
				uv[0][0], uv[0][1],
				argb, argb, argb
			);
		}

		// evaluate position
		VectorMA( p->org, frametime, p->vel, p->org );

		if( p->type == pt_grav )
		{
			p->vel[0] *= scale;
			p->vel[1] *= scale;
			p->vel[2] -= gravity;

			p->packedColor = 255 * (p->die - gp_cl->time) * 2;
			if( p->packedColor > 255 ) p->packedColor = 255;
		}
		else if( p->type == pt_slowgrav )
		{
			p->vel[2] = gravity * 0.05f;
		}
	}

	pvr_dr_finish();
}

/*
===============
CL_DrawParticlesExternal

allow to draw effects from custom renderer
===============
*/
void CL_DrawParticlesExternal( const ref_viewpass_t *rvp, qboolean trans_pass, float frametime )
{
	ref_instance_t	oldRI = RI;

	R_SetupRefParams( rvp );
	R_SetupFrustum();
	R_SetupGL( false );	// don't touch GL-states
	tr.frametime = frametime;

	gEngfuncs.CL_DrawEFX( frametime, trans_pass );

	// restore internal state
	RI = oldRI;
}
