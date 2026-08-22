/*
pvr_cull.c - render culling routines
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
#include "entity_types.h"
#include "pm_defs.h" // PM_* trace flags + pmtrace_t

/*
=============================================================

FRUSTUM AND PVS CULLING

=============================================================
*/
/*
=================
R_CullBox

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox( const vec3_t mins, const vec3_t maxs )
{
	return GL_FrustumCullBox( &RI.frustum, mins, maxs, 0 );
}

/*
=============
R_CullModel
=============
*/
int R_CullModel( cl_entity_t *e, const vec3_t absmin, const vec3_t absmax )
{
	if( e == tr.viewent )
	{
		if( ENGINE_GET_PARM( PARM_DEV_OVERVIEW ))
			return 1;

		if( RP_NORMALPASS() && !ENGINE_GET_PARM( PARM_THIRDPERSON ) && CL_IsViewEntityLocalPlayer())
			return 0;

		return 1;
	}

	if( R_CullBox( absmin, absmax ))
		return 1;

	// Occlusion culling for studio models: if the model is completely behind world solids
	// (as seen from the camera), skip it. This is NOT classic HL behavior, but useful on DC.
	// Conservative: only cull if multiple sample points are blocked.
	if( r_occlusion_cull_studio.value && e && e->model && e->model->type == mod_studio )
	{
		// If we don't have a sane view origin, don't try.
		// (RI.vieworg is set in setup; for safety keep this guard.)
		if( RI.drawWorld )
		{
			vec3_t center, top, bottom;
			VectorAverage( absmin, absmax, center );
			VectorCopy( center, top );
			VectorCopy( center, bottom );
			top[2] = absmax[2];
			bottom[2] = absmin[2];

			// IMPORTANT: do NOT use PM_WORLD_ONLY here, otherwise brush entities (func_door/func_rotating)
			// won't occlude and we'll still render models behind them.
			// Also ignore other studio models to avoid false occlusion chains.
			const int traceFlags = PM_GLASS_IGNORE | PM_STUDIO_IGNORE;

			// Prefer EV_VisTraceLine (returns pointer, used by sprite glow cull), fallback to CL_TraceLine.
			float *start = (float *)RI.vieworg;
			float *end_c = (float *)center;
			float *end_t = (float *)top;
			float *end_b = (float *)bottom;

			qboolean blocked_c = false, blocked_t = false, blocked_b = false;

			if( gEngfuncs.EV_VisTraceLine )
			{
				const pmtrace_t *tr_c = gEngfuncs.EV_VisTraceLine( start, end_c, traceFlags );
				blocked_c = ( tr_c && tr_c->fraction < 1.0f ) ? true : false;
				if( blocked_c )
				{
					const pmtrace_t *tr_t = gEngfuncs.EV_VisTraceLine( start, end_t, traceFlags );
					blocked_t = ( tr_t && tr_t->fraction < 1.0f ) ? true : false;
					if( blocked_t )
					{
						const pmtrace_t *tr_b = gEngfuncs.EV_VisTraceLine( start, end_b, traceFlags );
						blocked_b = ( tr_b && tr_b->fraction < 1.0f ) ? true : false;
					}
				}
			}
			else
			{
				const pmtrace_t tr_c = gEngfuncs.CL_TraceLine( RI.vieworg, center, traceFlags );
				blocked_c = ( tr_c.fraction < 1.0f ) ? true : false;
				if( blocked_c )
				{
					const pmtrace_t tr_t = gEngfuncs.CL_TraceLine( RI.vieworg, top, traceFlags );
					blocked_t = ( tr_t.fraction < 1.0f ) ? true : false;
					if( blocked_t )
					{
						const pmtrace_t tr_b = gEngfuncs.CL_TraceLine( RI.vieworg, bottom, traceFlags );
						blocked_b = ( tr_b.fraction < 1.0f ) ? true : false;
					}
				}
			}

			if( blocked_c && blocked_t && blocked_b )
			{
				return 1; // occluded
			}
		}
	}

	return 0;
}

/*
=================
R_CullSurface

cull invisible surfaces
=================
*/
int R_CullSurface( msurface_t *surf, gl_frustum_t *frustum, uint clipflags )
{
	cl_entity_t	*e = RI.currententity;

	if( !surf || !surf->texinfo || !surf->texinfo->texture )
		return CULL_OTHER;

	if( r_nocull.value )
		return CULL_VISIBLE;

	// world surfaces can be culled by vis frame too
	if( RI.currententity == CL_GetEntityByIndex( 0 ) && surf->visframe != tr.framecount )
		return CULL_VISFRAME;

	// only static ents can be culled by frustum
	if( !R_StaticEntity( e )) frustum = NULL;

	if( !VectorIsNull( surf->plane->normal ))
	{
		float	dist;

		// can use normal.z for world (optimisation)
		if( RI.drawOrtho )
		{
			vec3_t	orthonormal;

			if( e == CL_GetEntityByIndex( 0 )) orthonormal[2] = surf->plane->normal[2];
			else PVR_Mat4x4_VectorRotate( &RI.objectMatrix, surf->plane->normal, orthonormal );
			dist = orthonormal[2];
		}
		else dist = PlaneDiff( tr.modelorg, surf->plane );
		if( glState.faceCull == GL_FRONT )
		{
			if( FBitSet( surf->flags, SURF_PLANEBACK ))
			{
				if( dist >= -BACKFACE_EPSILON )
					return CULL_BACKSIDE; // wrong side
			}
			else
			{
				if( dist <= BACKFACE_EPSILON )
					return CULL_BACKSIDE; // wrong side
			}
		}
		else if( glState.faceCull == GL_BACK )
		{
			if( FBitSet( surf->flags, SURF_PLANEBACK ))
			{
				if( dist <= BACKFACE_EPSILON )
					return CULL_BACKSIDE; // wrong side
			}
			else
			{
				if( dist >= -BACKFACE_EPSILON )
					return CULL_BACKSIDE; // wrong side
			}
		}
	}
	if( frustum && GL_FrustumCullBox( frustum, surf->info->mins, surf->info->maxs, clipflags ))
		return CULL_FRUSTUM;

	return CULL_VISIBLE;
}
