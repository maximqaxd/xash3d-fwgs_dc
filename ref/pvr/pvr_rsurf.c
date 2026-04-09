/*
pvr_rsurf.c - surface-related refresh code
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
#include "xash3d_mathlib.h"
#include "mod_local.h"
#include <dc/pvr.h>
#include <sh4zam/shz_sh4zam.h>
#include "pvr_clip.h"

typedef struct
{
	int		allocated[BLOCK_SIZE_DEFAULT];
	int		current_lightmap_texture;
	msurface_t	*dynamic_surfaces;
	msurface_t	*lightmap_surfaces[MAX_LIGHTMAPS];
	byte		lightmap_buffer[BLOCK_SIZE_DEFAULT*BLOCK_SIZE_DEFAULT*LIGHTMAP_BPP];

} gllightmapstate_t;

static int		nColinElim; // stats
static vec2_t		world_orthocenter;
static vec2_t		world_orthohalf;
static uint		r_blocklights[BLOCK_SIZE_DEFAULT*BLOCK_SIZE_DEFAULT*3];
static mextrasurf_t		*fullbright_surfaces[MAX_TEXTURES];
static mextrasurf_t		*detail_surfaces[MAX_TEXTURES];
static int		rtable[MOD_FRAMES][MOD_FRAMES];

typedef struct
{
	int first, last;
} separate_pass_t;

static separate_pass_t draw_wateralpha = { 0, -1 };
static separate_pass_t draw_alpha_surfaces = { 0, -1 };
static separate_pass_t draw_fullbrights = { 0, -1 };
static separate_pass_t draw_details = { 0, -1 };
static msurface_t		*skychain = NULL;
static gllightmapstate_t	gl_lms;

static void LM_UploadBlock( qboolean dynamic );
static void R_RenderLightmapForSurface( msurface_t *fa );


static inline void R_AddToSeparatePass( separate_pass_t *sp, int num )
{
	if( sp->first > num )
		sp->first = num;

	if( sp->last < num )
		sp->last = num;
}

static inline void R_ResetSeparatePass( separate_pass_t *sp )
{
	sp->last = -1;
}

static inline qboolean R_SeparatePassActive( const separate_pass_t *sp )
{
	return sp->last >= 0 ? true : false;
}

byte *Mod_GetCurrentVis( void )
{
	if( gEngfuncs.drawFuncs->Mod_GetCurrentVis && tr.fCustomRendering )
		return gEngfuncs.drawFuncs->Mod_GetCurrentVis();
	return RI.visbytes;
}

void Mod_SetOrthoBounds( const float *mins, const float *maxs )
{
	if( gEngfuncs.drawFuncs->GL_OrthoBounds )
	{
		gEngfuncs.drawFuncs->GL_OrthoBounds( mins, maxs );
	}

	Vector2Average( maxs, mins, world_orthocenter );
	Vector2Subtract( maxs, world_orthocenter, world_orthohalf );
}

void R_LightmapCoord( const vec3_t v, const msurface_t *surf, const float sample_size, vec2_t coords )
{
	const mextrasurf_t *info = surf->info;
	float s, t;

	s = DotProduct( v, info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
	s += surf->light_s * sample_size;
	s += sample_size * 0.5f;
	s /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->width;

	t = DotProduct( v, info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];
	t += surf->light_t * sample_size;
	t += sample_size * 0.5f;
	t /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->width;

	Vector2Set( coords, s, t );
}

static void R_TextureCoord( const vec3_t v, const msurface_t *surf, vec2_t coords )
{
	const mtexinfo_t *info = surf->texinfo;
	float s, t;

	s = DotProduct( v, info->vecs[0] );
	t = DotProduct( v, info->vecs[1] );

	if( !FBitSet( surf->flags, SURF_DRAWTURB ))
	{
		s = ( s + info->vecs[0][3] ) / info->texture->width;
		t = ( t + info->vecs[1][3] ) / info->texture->height;
	}

	Vector2Set( coords, s, t );
}

static void R_GetEdgePosition( const model_t *mod, const msurface_t *fa, int i, vec3_t vec )
{
    int lindex;
    if( mod->surfedges16 ) lindex = (int)mod->surfedges16[fa->firstedge + i];
    else lindex = mod->surfedges[fa->firstedge + i];

	if( FBitSet( mod->flags, MODEL_QBSP2 ))
	{
		const medge32_t *pedges = mod->edges32;

		if( lindex > 0 )
			VectorCopy( mod->vertexes[pedges[lindex].v[0]].position, vec );
		else
			VectorCopy( mod->vertexes[pedges[-lindex].v[1]].position, vec );
	}
	else
	{
		const medge16_t *pedges = mod->edges16;

		if( lindex > 0 )
			VectorCopy( mod->vertexes[pedges[lindex].v[0]].position, vec );
		else
			VectorCopy( mod->vertexes[pedges[-lindex].v[1]].position, vec );
	}
}

static void BoundPoly( int numverts, float *verts, vec3_t mins, vec3_t maxs )
{
	int	i, j;
	float	*v;

	ClearBounds( mins, maxs );

	for( i = 0, v = verts; i < numverts; i++ )
	{
		for( j = 0; j < 3; j++, v++ )
		{
			if( *v < mins[j] ) mins[j] = *v;
			if( *v > maxs[j] ) maxs[j] = *v;
		}
	}
}

static void SubdividePolygon_r( model_t *loadmodel, msurface_t *warpface, int numverts, float *verts )
{
	vec3_t		front[SUBDIVIDE_SIZE], back[SUBDIVIDE_SIZE];
	float		dist[SUBDIVIDE_SIZE];
	float		m, frac, *v;
	int		i, j, k, f, b;
	float		sample_size;
	vec3_t		mins, maxs;
	glpoly2_t		*poly;

	if( numverts > ( SUBDIVIDE_SIZE - 4 ))
		gEngfuncs.Host_Error( "%s: too many vertexes on face ( %i )\n", __func__, numverts );

	sample_size = gEngfuncs.Mod_SampleSizeForFace( warpface );
	BoundPoly( numverts, verts, mins, maxs );

	for( i = 0; i < 3; i++ )
	{
		m = ( mins[i] + maxs[i] ) * 0.5f;
		m = SUBDIVIDE_SIZE * floor( m / SUBDIVIDE_SIZE + 0.5f );
		if( maxs[i] - m < 8 ) continue;
		if( m - mins[i] < 8 ) continue;

		// cut it
		v = verts + i;
		for( j = 0; j < numverts; j++, v += 3 )
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v -= i;
		VectorCopy( verts, v );

		f = b = 0;
		v = verts;
		for( j = 0; j < numverts; j++, v += 3 )
		{
			if( dist[j] >= 0 )
			{
				VectorCopy( v, front[f] );
				f++;
			}

			if( dist[j] <= 0 )
			{
				VectorCopy (v, back[b]);
				b++;
			}

			if( dist[j] == 0 || dist[j+1] == 0 )
				continue;

			if(( dist[j] > 0 ) != ( dist[j+1] > 0 ))
			{
				// clip point
				frac = dist[j] / ( dist[j] - dist[j+1] );
				for( k = 0; k < 3; k++ )
					front[f][k] = back[b][k] = v[k] + frac * (v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon_r( loadmodel, warpface, f, front[0] );
		SubdividePolygon_r( loadmodel, warpface, b, back[0] );
		return;
	}

	if( numverts != 4 )
		ClearBits( warpface->flags, SURF_DRAWTURB_QUADS );

	// add a point in the center to help keep warp valid
	poly = Mem_Calloc( loadmodel->mempool, sizeof( glpoly2_t ) + numverts * VERTEXSIZE * sizeof( float ));
	poly->next = warpface->polys;
	poly->flags = warpface->flags;
	warpface->polys = poly;
	poly->numverts = numverts;

	for( i = 0; i < numverts; i++, verts += 3 )
	{
		VectorCopy( verts, poly->verts[i] );
		R_TextureCoord( verts, warpface, &poly->verts[i][3] );

		// for speed reasons
		if( !FBitSet( warpface->flags, SURF_DRAWTURB ))
		{
			// lightmap texture coordinates
			R_LightmapCoord( verts, warpface, sample_size, &poly->verts[i][5] );
		}
	}
}

/*
===============================
GL_SetupFogColorForSurfaces

every render pass applies new fog layer, resulting in wrong fog color
recalculate fog color for current pass count
===============================
*/
static void GL_SetupFogColorForSurfacesEx( int passes, float density, qboolean blend_lightmaps )
{
	vec4_t	fogColor;
	float	factor, div;

	if( !glState.isFogEnabled )
		return;
#if 0
	if(( passes < 2 ) || (RI.currententity && RI.currententity->curstate.rendermode == kRenderTransTexture ))
	{
		pglFogfv( GL_FOG_COLOR, RI.fogColor );
		return;
	}

	div = passes - 1;
	factor = passes;
	fogColor[0] = pow( RI.fogColor[0] / div, ( 1.0f / factor ));
	fogColor[1] = pow( RI.fogColor[1] / div, ( 1.0f / factor ));
	fogColor[2] = pow( RI.fogColor[2] / div, ( 1.0f / factor ));
	fogColor[3] = 1.0f; // ignored but GL_FOG_COLOR requires vec4_t

	// because of enabled blending in R_BlendLightmaps, need to scale down fog color
	// but only during lightmap blending & without VBO (it takes another route)
	if( blend_lightmaps && gl_overbright.value )
		VectorScale( fogColor, 0.5f, fogColor );

	pglFogfv( GL_FOG_COLOR, fogColor );
	pglFogf( GL_FOG_DENSITY, RI.fogDensity * density );
#endif // TODO
}


void GL_SetupFogColorForSurfaces( void )
{
	GL_SetupFogColorForSurfacesEx( r_detailtextures.value ? 3 : 2, 1.0f, false );
}

void GL_ResetFogColor( void )
{
#if 0
	// restore fog here
	if( glState.isFogEnabled )
		pglFogfv( GL_FOG_COLOR, RI.fogColor );
#endif
}

/*
================
GL_SubdivideSurface

Breaks a polygon up along axial 64 unit
boundaries so that turbulent and sky warps
can be done reasonably.
================
*/
void GL_SubdivideSurface( model_t *loadmodel, msurface_t *fa )
{
	vec3_t	verts[SUBDIVIDE_SIZE];
	int	i;

	// convert edges back to a normal polygon
	for( i = 0; i < fa->numedges; i++ )
		R_GetEdgePosition( loadmodel, fa, i, verts[i] );

	SetBits( fa->flags, SURF_DRAWTURB_QUADS ); // predict state

	// do subdivide
	SubdividePolygon_r( loadmodel, fa, fa->numedges, verts[0] );
}

/*
================
GL_BuildPolygonFromSurface
================
*/
static int GL_BuildPolygonFromSurface( model_t *mod, msurface_t *fa )
{
	int		i, lnumverts, nColinElim = 0;
	float		sample_size;
	texture_t		*tex;
	gl_texture_t	*glt;
	glpoly2_t		*poly;

	if( !mod || !fa->texinfo || !fa->texinfo->texture )
		return nColinElim; // bad polygon ?

	if( FBitSet( fa->flags, SURF_CONVEYOR ) && fa->texinfo->texture->gl_texturenum != 0 )
	{
		glt = R_GetTexture( fa->texinfo->texture->gl_texturenum );
		tex = fa->texinfo->texture;
		Assert( glt != NULL && tex != NULL );

		// update conveyor widths for keep properly speed of scrolling
		glt->srcWidth = tex->width;
		glt->srcHeight = tex->height;
	}

	sample_size = gEngfuncs.Mod_SampleSizeForFace( fa );

	// reconstruct the polygon
	lnumverts = fa->numedges;

	// detach if already created, reconstruct again
	poly = fa->polys;
	fa->polys = NULL;

	// quake simple models (healthkits etc) need to be reconstructed their polys because LM coords has changed after the map change
	poly = Mem_Realloc( mod->mempool, poly, sizeof( glpoly2_t ) + lnumverts * VERTEXSIZE * sizeof( float ));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for( i = 0; i < lnumverts; i++ )
	{
		R_GetEdgePosition( mod, fa, i, poly->verts[i] );
		R_TextureCoord( poly->verts[i], fa, &poly->verts[i][3] );
		R_LightmapCoord( poly->verts[i], fa, sample_size, &poly->verts[i][5] );
	}

	// remove co-linear points - Ed
	if( !gl_keeptjunctions.value && !FBitSet( fa->flags, SURF_UNDERWATER ))
	{
		for( i = 0; i < lnumverts; i++ )
		{
			vec3_t	v1, v2;
			float	*prev, *this, *next;

			prev = poly->verts[(i + lnumverts - 1) % lnumverts];
			next = poly->verts[(i + 1) % lnumverts];
			this = poly->verts[i];

			VectorSubtract( this, prev, v1 );
			VectorNormalize( v1 );
			VectorSubtract( next, prev, v2 );
			VectorNormalize( v2 );

			// skip co-linear points
			if(( fabs( v1[0] - v2[0] ) <= 0.001f) && (fabs( v1[1] - v2[1] ) <= 0.001f) && (fabs( v1[2] - v2[2] ) <= 0.001f))
			{
				int	j, k;

				for( j = i + 1; j < lnumverts; j++ )
				{
					for( k = 0; k < VERTEXSIZE; k++ )
						poly->verts[j-1][k] = poly->verts[j][k];
				}

				// retry next vertex next time, which is now current vertex
				lnumverts--;
				nColinElim++;
				i--;
			}
		}
	}

	poly->numverts = lnumverts;
	return nColinElim;
}

/*
===============
R_TextureAnim

Returns the proper texture for a given time and base texture, do not process random tiling
===============
*/
static texture_t *R_TextureAnim( texture_t *b )
{
	texture_t *base = b;
	int	count, reletive;

	if( RI.currententity->curstate.frame )
	{
		if( base->alternate_anims )
			base = base->alternate_anims;
	}

	if( !base->anim_total )
		return base;

	if( base->name[0] == '-' )
	{
		return b; // already tiled
	}
	else
	{
		int	speed;

		// Quake1 textures uses 10 frames per second
		if( FBitSet( R_GetTexture( base->gl_texturenum )->flags, TF_QUAKEPAL ))
			speed = 10;
		else speed = 20;

		reletive = (int)(gp_cl->time * speed) % base->anim_total;
	}


	count = 0;

	while( base->anim_min > reletive || base->anim_max <= reletive )
	{
		base = base->anim_next;

		if( !base || ++count > MOD_FRAMES )
			return b;
	}

	return base;
}

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and surface
===============
*/
static texture_t *R_TextureAnimation( msurface_t *s )
{
	texture_t	*base = s->texinfo->texture;
	int	count, reletive;

	if( RI.currententity && RI.currententity->curstate.frame )
	{
		if( base->alternate_anims )
			base = base->alternate_anims;
	}

	if( !base->anim_total )
		return base;

	if( base->name[0] == '-' )
	{
		int	tx = (int)((s->texturemins[0] + (base->width << 16)) / base->width) % MOD_FRAMES;
		int	ty = (int)((s->texturemins[1] + (base->height << 16)) / base->height) % MOD_FRAMES;

		reletive = rtable[tx][ty] % base->anim_total;
	}
	else
	{
		int	speed;

		// Quake1 textures uses 10 frames per second
		if( FBitSet( R_GetTexture( base->gl_texturenum )->flags, TF_QUAKEPAL ))
			speed = 10;
		else speed = 20;

		reletive = (int)(gp_cl->time * speed) % base->anim_total;
	}

	count = 0;

	while( base->anim_min > reletive || base->anim_max <= reletive )
	{
		base = base->anim_next;

		if( !base || ++count > MOD_FRAMES )
			return s->texinfo->texture;
	}

	return base;
}

/*
===============
R_AddDynamicLights
===============
*/
static void R_AddDynamicLights( const msurface_t *surf )
{
	const mextrasurf_t *info = surf->info;
	int lnum, smax, tmax;
	int sample_frac = 1.0;
	float sample_size;
	mtexinfo_t *tex;

	// no dlighted surfaces here
	if( !surf->dlightbits )
		return;

	sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
	smax = (info->lightextents[0] / sample_size) + 1;
	tmax = (info->lightextents[1] / sample_size) + 1;
	tex = surf->texinfo;

	if( FBitSet( tex->flags, TEX_WORLD_LUXELS ))
	{
		if( surf->texinfo->faceinfo )
			sample_frac = surf->texinfo->faceinfo->texture_step;
		else if( FBitSet( surf->texinfo->flags, TEX_EXTRA_LIGHTMAP ))
			sample_frac = LM_SAMPLE_EXTRASIZE;
		else sample_frac = LM_SAMPLE_SIZE;
	}

	for( lnum = 0; lnum < MAX_DLIGHTS; lnum++ )
	{
		dlight_t *dl;
		vec3_t impact, origin_l;
		float dist, rad, minlight;
		float sl, tl;
		int t;

		if( !FBitSet( surf->dlightbits, BIT( lnum )))
			continue;	// not lit by this light

		dl = &tr.dlights[lnum];

		// transform light origin to local bmodel space
		if( !tr.modelviewIdentity )
			PVR_Mat4x4_VectorITransform( &RI.objectMatrix, dl->origin, origin_l );
		else VectorCopy( dl->origin, origin_l );

		rad = dl->radius;
		dist = PlaneDiff( origin_l, surf->plane );
		rad -= fabs( dist );

		// rad is now the highest intensity on the plane
		minlight = dl->minlight;
		if( rad < minlight )
			continue;

		minlight = rad - minlight;

		if( surf->plane->type < 3 )
		{
			VectorCopy( origin_l, impact );
			impact[surf->plane->type] -= dist;
		}
		else VectorMA( origin_l, -dist, surf->plane->normal, impact );

		sl = DotProduct( impact, info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
		tl = DotProduct( impact, info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];

		for( t = 0; t < tmax; t++ )
		{
			int td = (tl - sample_size * t) * sample_frac;
			int s;

			if( td < 0 )
				td = -td;

			for( s = 0; s < smax; s++ )
			{
				int sd = (sl - sample_size * s) * sample_frac;
				float dist;

				if( sd < 0 )
					sd = -sd;

				if( sd > td )
					dist = sd + (td >> 1);
				else
					dist = td + (sd >> 1);

				if( dist < minlight )
				{
					uint *bl = &r_blocklights[(s + (t * smax)) * 3];

					bl[0] += ((int)((rad - dist) * 256) * dl->color.r ) / 256;
					bl[1] += ((int)((rad - dist) * 256) * dl->color.g ) / 256;
					bl[2] += ((int)((rad - dist) * 256) * dl->color.b ) / 256;
				}
			}
		}
	}
}

/*
================
R_SetCacheState
================
*/
static void R_SetCacheState( msurface_t *surf )
{
	(void)surf;
}

/*
=============================================================================

  LIGHTMAP ALLOCATION

=============================================================================
*/
static void LM_InitBlock( void )
{
	memset( gl_lms.allocated, 0, sizeof( gl_lms.allocated ));
}

static int LM_AllocBlock( int w, int h, int *x, int *y )
{
	int	i, j;
	int	best, best2;

	best = BLOCK_SIZE;

	for( i = 0; i < BLOCK_SIZE - w; i++ )
	{
		best2 = 0;

		for( j = 0; j < w; j++ )
		{
			if( gl_lms.allocated[i+j] >= best )
				break;
			if( gl_lms.allocated[i+j] > best2 )
				best2 = gl_lms.allocated[i+j];
		}

		if( j == w )
		{
			// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if( best + h > BLOCK_SIZE )
		return false;

	for( i = 0; i < w; i++ )
		gl_lms.allocated[*x + i] = best + h;

	return true;
}

static void LM_UploadDynamicBlock( void )
{
	int	height = 0, i;

	for( i = 0; i < BLOCK_SIZE; i++ )
	{
		if( gl_lms.allocated[i] > height )
			height = gl_lms.allocated[i];
	}
#if 0	
	pglTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, BLOCK_SIZE, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, gl_lms.lightmap_buffer );
#endif // TODO
}

static void LM_UploadBlock( qboolean dynamic )
{
	if( dynamic )
	{
		GL_Bind( XASH_TEXTURE0, tr.dlightTexture );
		LM_UploadDynamicBlock();
	}
	else
	{
		rgbdata_t	r_lightmap;
		char	lmName[16];
		int i = gl_lms.current_lightmap_texture;

		// upload static lightmaps only during loading
		memset( &r_lightmap, 0, sizeof( r_lightmap ));
		Q_snprintf( lmName, sizeof( lmName ), "*lightmap%i", i );

		r_lightmap.width = BLOCK_SIZE;
		r_lightmap.height = BLOCK_SIZE;
		r_lightmap.type = LIGHTMAP_FORMAT;
		r_lightmap.size = r_lightmap.width * r_lightmap.height * LIGHTMAP_BPP;
		r_lightmap.flags = IMAGE_HAS_COLOR;
		r_lightmap.buffer = gl_lms.lightmap_buffer;
		tr.lightmapTextures[i] = GL_LoadTextureInternal( lmName, &r_lightmap, TF_NOMIPMAP|TF_ATLAS_PAGE );
		
		if( ++gl_lms.current_lightmap_texture == MAX_LIGHTMAPS )
			gEngfuncs.Host_Error( "%s: full\n", __func__ );
	}
}

/*
=================
R_BuildLightmap

Combine and scale multiple lightmaps into the floating
format in r_blocklights
=================
*/
static void R_BuildLightMap( const msurface_t *surf, byte *dest, int stride, qboolean dynamic )
{
	int map, t;
	const mextrasurf_t *info = surf->info;
	int lightscale;
	int s;

	const int sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
	const int smax = ( info->lightextents[0] / sample_size ) + 1;
	const int tmax = ( info->lightextents[1] / sample_size ) + 1;
	const int size = smax * tmax;

	if( gl_overbright.value )
		lightscale = 256;
	else lightscale = ( pow( 2.0f, 1.0f / v_lightgamma->value ) * 256 ) + 0.5;

	memset( r_blocklights, 0, sizeof( uint ) * size * 3 );

	/* add all the lightmaps (BSP samples or LT2) */
	for( map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++ )
	{
		const uint scale = tr.lightstylevalue[surf->styles[map]];

		if( surf->samples )
		{
			const color24 *lm = &surf->samples[map * size];
			for( int i = 0; i < size; i++ )
			{
				r_blocklights[i * 3 + 0] += lm[i].r * scale;
				r_blocklights[i * 3 + 1] += lm[i].g * scale;
				r_blocklights[i * 3 + 2] += lm[i].b * scale;
			}
		}
		else if( WORLDMODEL && FBitSet( WORLDMODEL->flags, MODEL_LT2_LIGHTING ) &&
		         WORLDMODEL->lt2_payload && WORLDMODEL->lt2_lightsurfs && info )
		{
			/* LT2 'a' (LERP control-point grid) evaluated on demand */
			const int face_index = info->lt2_face_index;
			if( face_index >= 0 && (uint32_t)face_index < WORLDMODEL->lt2_lightsurfs_count )
			{
				/* locate style grid by walking previous grids */
				uint32_t pos = WORLDMODEL->lt2_lightsurfs[face_index];
				for( int st = 0; st < map; st++ )
				{
					if( pos + 1u >= WORLDMODEL->lt2_payload_size ) { pos = 0; break; }
					const byte op = WORLDMODEL->lt2_payload[pos];
					const uint gw = ((op >> 4) & 0x0F) + 2u;
					const uint gh = (op & 0x0F) + 2u;
					pos += 1u + gw * gh * 3u;
				}

				if( pos + 1u < WORLDMODEL->lt2_payload_size )
				{
					const byte op = WORLDMODEL->lt2_payload[pos];
					const uint gw = ((op >> 4) & 0x0F) + 2u;
					const uint gh = (op & 0x0F) + 2u;
					const uint need = 1u + gw * gh * 3u;
					const byte *grid = WORLDMODEL->lt2_payload + pos + 1u;

					if( pos + need <= WORLDMODEL->lt2_payload_size && gw >= 2 && gh >= 2 )
					{
						for( int y = 0; y < tmax; y++ )
						{
							const float v = (tmax == 1) ? 0.0f : ((float)y / (float)(tmax - 1)) * (float)(gh - 1);
							int cy = (int)floorf( v );
							if( cy < 0 ) cy = 0;
							if( cy > (int)gh - 2 ) cy = (int)gh - 2;
							const float fy = v - (float)cy;

							for( int x = 0; x < smax; x++ )
							{
								const float u = (smax == 1) ? 0.0f : ((float)x / (float)(smax - 1)) * (float)(gw - 1);
								int cx = (int)floorf( u );
								if( cx < 0 ) cx = 0;
								if( cx > (int)gw - 2 ) cx = (int)gw - 2;
								const float fx = u - (float)cx;

								const uint idx00 = (uint)((cy * (int)gw + cx) * 3);
								const uint idx10 = (uint)((cy * (int)gw + (cx + 1)) * 3);
								const uint idx01 = (uint)(((cy + 1) * (int)gw + cx) * 3);
								const uint idx11 = (uint)(((cy + 1) * (int)gw + (cx + 1)) * 3);

								const float c00r = (float)grid[idx00 + 0], c00g = (float)grid[idx00 + 1], c00b = (float)grid[idx00 + 2];
								const float c10r = (float)grid[idx10 + 0], c10g = (float)grid[idx10 + 1], c10b = (float)grid[idx10 + 2];
								const float c01r = (float)grid[idx01 + 0], c01g = (float)grid[idx01 + 1], c01b = (float)grid[idx01 + 2];
								const float c11r = (float)grid[idx11 + 0], c11g = (float)grid[idx11 + 1], c11b = (float)grid[idx11 + 2];

								const float top_r = c00r + (c10r - c00r) * fx;
								const float top_g = c00g + (c10g - c00g) * fx;
								const float top_b = c00b + (c10b - c00b) * fx;
								const float bot_r = c01r + (c11r - c01r) * fx;
								const float bot_g = c01g + (c11g - c01g) * fx;
								const float bot_b = c01b + (c11b - c01b) * fx;

								const int i = x + y * smax;
								r_blocklights[i * 3 + 0] += (uint)(top_r + (bot_r - top_r) * fy) * scale;
								r_blocklights[i * 3 + 1] += (uint)(top_g + (bot_g - top_g) * fy) * scale;
								r_blocklights[i * 3 + 2] += (uint)(top_b + (bot_b - top_b) * fy) * scale;
							}
						}
					}
				}
			}
		}
	}

	// add all the dynamic lights
	if( surf->dlightframe == tr.framecount && dynamic )
		R_AddDynamicLights( surf );

	  #if LIGHTMAP_BPP == 2
        // RGB565 format: each pixel is 2 bytes
        for (t = 0; t < tmax; t++) {
            for (s = 0; s < smax; s++) {
                const uint *bl = &r_blocklights[(s + (t * smax)) * 3];
                uint16_t *dst = (uint16_t*)&dest[(t * stride) + (s * 2)];

                uint8_t r = 0, g = 0, b = 0;
                for (int i = 0; i < 3; i++) {
                    int val = bl[i] * lightscale >> 14;
                    if (val > 1023)
                        val = 1023;
                    uint8_t gammaCorrected = LightToTexGamma(val) >> 2;

                    if (i == 0) r = gammaCorrected;
                    else if (i == 1) g = gammaCorrected;
                    else if (i == 2) b = gammaCorrected;
                }

                // Pack RGB565
                *dst = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            }
        }
    #elif LIGHTMAP_BPP == 3
        // RGB24 format: each pixel is 3 bytes
        for (t = 0; t < tmax; t++) {
            for (s = 0; s < smax; s++) {
                const uint *bl = &r_blocklights[(s + (t * smax)) * 3];
                byte *dst = &dest[(t * stride) + (s * 3)];

                for (int i = 0; i < 3; i++) {
                    int val = bl[i] * lightscale >> 14;
                    if (val > 1023)
                        val = 1023;
                    dst[i] = LightToTexGamma(val) >> 2;
                }
            }
        }
    #elif LIGHTMAP_BPP == 4
        // RGBA32 format: each pixel is 4 bytes
        for (t = 0; t < tmax; t++) {
            for (s = 0; s < smax; s++) {
                const uint *bl = &r_blocklights[(s + (t * smax)) * 3];
                byte *dst = &dest[(t * stride) + (s * 4)];

                for (int i = 0; i < 3; i++) {
                    int val = bl[i] * lightscale >> 14;
                    if (val > 1023)
                        val = 1023;
                    dst[i] = LightToTexGamma(val) >> 2;
                }
                dst[3] = 255; // Alpha channel
            }
        }
    #else
        #error "Unsupported LIGHTMAP_BPP value"
    #endif
}


/*
================
AddDynamicLightToVertex
Q2-style: 3D distance from vertex to light, add (radius - dist) * color.
================
*/
static void AddDynamicLightToVertex( const msurface_t *surf, const vec3_t vert_world,
	uint *r_accum, uint *g_accum, uint *b_accum )
{
	if( !surf->dlightbits || surf->dlightframe != tr.framecount || !r_dynamic->value )
		return;

	vec3_t impact;
	for( int lnum = 0; lnum < MAX_DLIGHTS; lnum++ )
	{
		if( !FBitSet( surf->dlightbits, BIT( lnum )))
			continue;

		const dlight_t *dl = &tr.dlights[lnum];
		if( dl->die < gp_cl->time || !dl->radius )
			continue;

		VectorSubtract( dl->origin, vert_world, impact );
		float dist = VectorLength( impact );
		float add = dl->radius - dist;
		if( add <= 0.0f )
			continue;
		if( add < dl->minlight )
			continue;

		uint add_scaled = (uint)( add * 256.0f );
		*r_accum += ( dl->color.r * add_scaled ) / 256;
		*g_accum += ( dl->color.g * add_scaled ) / 256;
		*b_accum += ( dl->color.b * add_scaled ) / 256;
	}

	const uint max_light_accum = 255 * 256;
	if( *r_accum > max_light_accum ) *r_accum = max_light_accum;
	if( *g_accum > max_light_accum ) *g_accum = max_light_accum;
	if( *b_accum > max_light_accum ) *b_accum = max_light_accum;
}

/*
================
SampleVertexLight
Q2-style: nearest-neighbor static sample + LT2 single sample + simple 3D dlights.
================
*/
static uint32_t SampleVertexLight( const msurface_t *surf, const float *vert )
{
#if REF_PVR_PROFILE
	PVR_Prof_Start();
#endif
	uint r_accum = 0, g_accum = 0, b_accum = 0;
	qboolean has_static = false;
	vec3_t vert_world;

	// World-space vertex for dlights (only when needed)
	qboolean need_dlights = ( surf && surf->dlightbits && surf->dlightframe == tr.framecount );
	if( need_dlights )
	{
		if( tr.modelviewIdentity )
			VectorCopy( vert, vert_world );
		else
			PVR_Mat4x4_TransformVec3( &RI.objectMatrix, vert, vert_world );
	}

	// Static lightmap (BSP samples or LT2) — nearest-neighbor single sample
	if( surf && WORLDMODEL && ( ( surf->samples && WORLDMODEL->lightdata ) || FBitSet( WORLDMODEL->flags, MODEL_LT2_LIGHTING )))
	{
		const mextrasurf_t *info = surf->info;
#if XASH_DREAMCAST
		SHZ_PREFETCH( info->lmvecs );
#endif
		const int sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
		const int smax = ( info->lightextents[0] / sample_size ) + 1;
		const int tmax = ( info->lightextents[1] / sample_size ) + 1;
		const int size = smax * tmax;

#if XASH_DREAMCAST
		shz_vec2_t st = shz_vec3_dot2( shz_vec3_deref( vert ),
			shz_vec3_deref( info->lmvecs[0] ), shz_vec3_deref( info->lmvecs[1] ));
		float s = st.x + info->lmvecs[0][3] - info->lightmapmins[0];
		float t = st.y + info->lmvecs[1][3] - info->lightmapmins[1];
#else
		float s = DotProduct( vert, info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
		float t = DotProduct( vert, info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];
#endif

		float ls = s / (float)sample_size;
		float lt = t / (float)sample_size;
		int lux_s = (int)ls;
		int lux_t = (int)lt;
		if( lux_s < 0 ) lux_s = 0;
		if( lux_s >= smax ) lux_s = smax - 1;
		if( lux_t < 0 ) lux_t = 0;
		if( lux_t >= tmax ) lux_t = tmax - 1;

		for( int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++ )
		{
			const uint scale = tr.lightstylevalue[surf->styles[maps]];

			if( surf->samples && WORLDMODEL->lightdata )
			{
				const color24 *p = &surf->samples[maps * size + lux_t * smax + lux_s];
#if XASH_DREAMCAST
				SHZ_PREFETCH( p );
#endif
				r_accum += (uint)p->r * scale;
				g_accum += (uint)p->g * scale;
				b_accum += (uint)p->b * scale;
				has_static = true;
			}
			else if( FBitSet( WORLDMODEL->flags, MODEL_LT2_LIGHTING ) && WORLDMODEL->lt2_payload && WORLDMODEL->lt2_lightsurfs && info )
			{
				const int face_index = info->lt2_face_index;
				if( face_index >= 0 && (uint32_t)face_index < WORLDMODEL->lt2_lightsurfs_count )
				{
					uint32_t pos = WORLDMODEL->lt2_lightsurfs[face_index];
					for( int m = 0; m < maps; m++ )
					{
						if( pos + 1u >= WORLDMODEL->lt2_payload_size ) { pos = 0; break; }
						const byte op = WORLDMODEL->lt2_payload[pos];
						const uint gw = ((op >> 4) & 0x0F) + 2u;
						const uint gh = (op & 0x0F) + 2u;
						pos += 1u + gw * gh * 3u;
					}
					if( pos + 1u < WORLDMODEL->lt2_payload_size )
					{
						const byte op = WORLDMODEL->lt2_payload[pos];
						const uint gw = ((op >> 4) & 0x0F) + 2u;
						const uint gh = (op & 0x0F) + 2u;
						const uint need = 1u + gw * gh * 3u;
						const byte *grid = WORLDMODEL->lt2_payload + pos + 1u;
						if( pos + need <= WORLDMODEL->lt2_payload_size && gw >= 1 && gh >= 1 )
						{
							float u = (smax <= 1) ? 0.0f : (ls / (float)(smax - 1)) * (float)(gw - 1);
							float v = (tmax <= 1) ? 0.0f : (lt / (float)(tmax - 1)) * (float)(gh - 1);
							int cx = (int)u;
							int cy = (int)v;
							if( cx < 0 ) cx = 0;
							if( cx >= (int)gw ) cx = (int)gw - 1;
							if( cy < 0 ) cy = 0;
							if( cy >= (int)gh ) cy = (int)gh - 1;
							uint idx = (uint)( cy * gw + cx ) * 3u;
							r_accum += (uint)grid[idx + 0] * scale;
							g_accum += (uint)grid[idx + 1] * scale;
							b_accum += (uint)grid[idx + 2] * scale;
							has_static = true;
						}
					}
				}
			}
		}
	}

	// Add dynamic lights (Q2-style 3D distance; vert_world set only when need_dlights)
	if( need_dlights )
		AddDynamicLightToVertex( surf, vert_world, &r_accum, &g_accum, &b_accum );

	// No static and no dlight contribution -> fullbright
	if( !has_static && r_accum == 0 && g_accum == 0 && b_accum == 0 )
	{
#if REF_PVR_PROFILE
		r_stats.t_world_lighting += PVR_Prof_End();
#endif
		return 0xFF000000 | (255 << 16) | (255 << 8) | 255;
	}

	// Apply lightscale (gamma compensation) and convert to 10-bit range
	// This matches GL renderer: val = bl[i] * lightscale >> 14
	int lightscale;
	if( gl_overbright.value )
		lightscale = 256; // Simple case for overbright
	else
		lightscale = (int)( pow( 2.0f, 1.0f / v_lightgamma->value ) * 256.0f + 0.5f );

	uint r_val = ( r_accum * lightscale ) >> 14;
	uint g_val = ( g_accum * lightscale ) >> 14;
	uint b_val = ( b_accum * lightscale ) >> 14;
	if( r_val > 1023 ) r_val = 1023;
	if( g_val > 1023 ) g_val = 1023;
	if( b_val > 1023 ) b_val = 1023;

	// Gamma to 8-bit
	int ir, ig, ib;
	if( FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ))
	{
		ir = r_val >> 2;
		ig = g_val >> 2;
		ib = b_val >> 2;
	}
	else
	{
		ir = tr.lightgammatable[r_val] >> 2;
		ig = tr.lightgammatable[g_val] >> 2;
		ib = tr.lightgammatable[b_val] >> 2;
	}
	if( ir > 255 ) ir = 255;
	if( ig > 255 ) ig = 255;
	if( ib > 255 ) ib = 255;

#if REF_PVR_PROFILE
	r_stats.t_world_lighting += PVR_Prof_End();
#endif
	return 0xFF000000 | (ir << 16) | (ig << 8) | ib;
}

/*
================
DrawGLPolyVertices
Helper function to submit polygon vertices to PVR
Now supports per-vertex colors for Gouraud shading
================
*/
#if XASH_DREAMCAST
static SHZ_HOT void DrawGLPolyVertices( glpoly2_t *p, pvr_dr_state_t *dr_state, const uint32_t *vertex_colors, float sOffset, float tOffset, float xScale, float yScale )
#else
static void DrawGLPolyVertices( glpoly2_t *p, pvr_dr_state_t *dr_state, const uint32_t *vertex_colors, float sOffset, float tOffset, float xScale, float yScale )
#endif
{
	if( !p || p->numverts < 3 ) return;

	float *v = p->verts[0];
	const int numverts = p->numverts;

	// Load matrix once
	__attribute__((aligned(8))) float aligned_matrix[16];
	memcpy( aligned_matrix, r_world_matrix, sizeof( aligned_matrix ));
#if REF_PVR_PROFILE
	PVR_Prof_Start();
#endif
	shz_xmtrx_load_4x4((shz_mat4x4_t*)aligned_matrix);

	// Transform all vertices and compute 1/w in same pass (cache-hot; avoids second pass in all-visible path).
	shz_vec4_t transformed[64];
	float inv_w[64];
	float uv[64][2];
	unsigned vismask_all = 0;
	float batch_4x[16];

	int hasScale = (xScale != 0.0f && yScale != 0.0f);

	int j = 0;
	for( ; j + 4 <= numverts; j += 4 )
	{
		for( int k = 0; k < 4; k++ )
		{
			float *vk = v + (j + k) * VERTEXSIZE;
			batch_4x[k*4 + 0] = vk[0];
			batch_4x[k*4 + 1] = vk[1];
			batch_4x[k*4 + 2] = vk[2];
			batch_4x[k*4 + 3] = 1.0f;
		}
		shz_xmtrx_load_apply_unaligned_4x4( aligned_matrix, batch_4x );
		for( int k = 0; k < 4; k++ )
		{
			const int i = j + k;
			shz_vec4_t clip = shz_xmtrx_read_col( k );
			transformed[i] = clip;
			inv_w[i] = shz_invf_fsrra( clip.w );
			if( clip.w >= clip.z + PVR_NEAR_CLIP_EPSILON )
				vismask_all |= (1u << i);
			float *vk = v + i * VERTEXSIZE;
			float s = vk[3] + sOffset;
			float t = vk[4] + tOffset;
			if( hasScale ) { s *= xScale; t *= yScale; }
			uv[i][0] = s;
			uv[i][1] = t;
		}
		shz_xmtrx_load_4x4( (shz_mat4x4_t *)aligned_matrix );
	}
	for( ; j < numverts; j++ )
	{
#if XASH_DREAMCAST
		if( j + 1 < numverts )
			SHZ_PREFETCH( v + (j + 1) * VERTEXSIZE );
#endif
		float *vj = v + j * VERTEXSIZE;
		shz_vec3_t pos = shz_vec3_init( vj[0], vj[1], vj[2] );
		shz_vec4_t clip = shz_xmtrx_transform_vec4( shz_vec3_vec4( pos, 1.0f ));
		transformed[j] = clip;
		inv_w[j] = shz_invf_fsrra( clip.w );
		if( clip.w >= clip.z + PVR_NEAR_CLIP_EPSILON )
			vismask_all |= (1u << j);
		float s = vj[3] + sOffset;
		float t = vj[4] + tOffset;
		if( hasScale ) { s *= xScale; t *= yScale; }
		uv[j][0] = s;
		uv[j][1] = t;
	}

#if REF_PVR_PROFILE
	r_stats.t_world_transforms += PVR_Prof_End();
	PVR_Prof_Start(); // Start geometry profiling
#endif

	// Early out if entire poly is behind near plane
	if( vismask_all == 0 )
	{
#if REF_PVR_PROFILE
		PVR_Prof_End(); // Cancel geometry profiling if nothing to submit
#endif
		return;
	}

	// Check if all visible (common case)
	unsigned all_visible_mask = (1u << numverts) - 1;

	if( vismask_all == all_visible_mask )
	{
		// Fast path: inv_w already computed in transform loop (cache-hot)

		// Fan: tri (0,1,2), (0,2,3), ... — must emit 3 verts per tri (strip would give wrong tris)
		for( int i = 1; i < numverts - 1; i++ )
		{
			const float inv_w0 = inv_w[0];
			const float inv_wi = inv_w[i];
			const float inv_wi1 = inv_w[i+1];

			pvr_vertex_t *vert = pvr_dr_target(*dr_state);
			vert->flags = PVR_CMD_VERTEX;
			vert->x = transformed[0].x * inv_w0;
			vert->y = transformed[0].y * inv_w0;
			vert->z = inv_w0;
			vert->u = uv[0][0];
			vert->v = uv[0][1];
			vert->argb = vertex_colors ? vertex_colors[0] : 0xFFFFFFFF;
			vert->oargb = 0;
			pvr_dr_commit(vert);

			vert = pvr_dr_target(*dr_state);
			vert->flags = PVR_CMD_VERTEX;
			vert->x = transformed[i].x * inv_wi;
			vert->y = transformed[i].y * inv_wi;
			vert->z = inv_wi;
			vert->u = uv[i][0];
			vert->v = uv[i][1];
			vert->argb = vertex_colors ? vertex_colors[i] : 0xFFFFFFFF;
			vert->oargb = 0;
			pvr_dr_commit(vert);

			vert = pvr_dr_target(*dr_state);
			vert->flags = PVR_CMD_VERTEX_EOL;
			vert->x = transformed[i+1].x * inv_wi1;
			vert->y = transformed[i+1].y * inv_wi1;
			vert->z = inv_wi1;
			vert->u = uv[i+1][0];
			vert->v = uv[i+1][1];
			vert->argb = vertex_colors ? vertex_colors[i+1] : 0xFFFFFFFF;
			vert->oargb = 0;
			pvr_dr_commit(vert);
		}
	}
	else
	{
		// Slow path: clip triangles against near plane.
		for( int i = 1; i < numverts - 1; i++ )
		{
			PVR_ClipAndSubmitTriangle(
				dr_state,
				transformed[0], transformed[i], transformed[i+1],
				uv[0][0], uv[0][1],
				uv[i][0], uv[i][1],
				uv[i+1][0], uv[i+1][1],
				vertex_colors ? vertex_colors[0] : 0xFFFFFFFF,
				vertex_colors ? vertex_colors[i] : 0xFFFFFFFF,
				vertex_colors ? vertex_colors[i+1] : 0xFFFFFFFF
			);
		}
	}

#if REF_PVR_PROFILE
	r_stats.t_world_geometry += PVR_Prof_End();
#endif
}

/*
================
DrawGLPoly_AnyVertexVisible

Quick visibility test used to avoid submitting a polygon header with zero
vertices afterwards 
================
*/
static qboolean DrawGLPoly_AnyVertexVisible( glpoly2_t *p )
{
	if( !p || p->numverts < 3 )
		return false;

	__attribute__((aligned(8))) float aligned_matrix[16];
	memcpy( aligned_matrix, r_world_matrix, sizeof( aligned_matrix ));
	shz_xmtrx_load_4x4((shz_mat4x4_t*)aligned_matrix);

	float *v = p->verts[0];
	const int numverts = p->numverts;

	for( int i = 0; i < numverts; i++, v += VERTEXSIZE )
	{
		shz_vec3_t pos = shz_vec3_init( v[0], v[1], v[2] );
		shz_vec4_t tp = shz_xmtrx_transform_vec4( shz_vec3_vec4( pos, 1.0f ));
		if( tp.w >= tp.z + PVR_NEAR_CLIP_EPSILON )
			return true;
	}

	return false;
}

/*
================
DrawGLPolySurfaceGouraud
Gouraud shaded polygon - samples light per vertex from surf->samples 
================
*/
static SHZ_HOT void DrawGLPolySurfaceGouraud( glpoly2_t *p, pvr_dr_state_t *dr_state, const msurface_t *surf, float sOffset, float tOffset, float xScale, float yScale )
{
	if( !p || p->numverts < 3 || !surf )
		return;

	float *v = p->verts[0];
	const int numverts = p->numverts;

#if XASH_DREAMCAST
	// Prefetch current surface's lmvecs so SampleVertexLight has it hot
	if( surf->info )
		SHZ_PREFETCH( surf->info->lmvecs );
#endif

	// Sample light per vertex from surf->samples
	uint32_t colors[64];
	for( int i = 0; i < numverts && i < 64; i++, v += VERTEXSIZE )
	{
#if XASH_DREAMCAST
		// Prefetch next vertex so it's in cache for the next iteration
		if( i + 1 < numverts && i + 1 < 64 )
			SHZ_PREFETCH( v + VERTEXSIZE );
#endif
		colors[i] = SampleVertexLight( surf, v );
	}
	
	// Call DrawGLPolyVertices with per-vertex colors
	DrawGLPolyVertices( p, dr_state, colors, sOffset, tOffset, xScale, yScale );
}

/*
================
DrawGLPoly
================
*/
// If dr_state is NULL, DrawGLPoly will create its own DR session (for entity rendering).
// If dr_state is non-NULL, it uses the shared DR state (for texture chain batching).
static void DrawGLPoly( glpoly2_t *p, float xScale, float yScale, const msurface_t *surf, pvr_dr_state_t *shared_dr_state )
{
	float		sOffset, sy;
	float		tOffset, cy;
	cl_entity_t	*e = RI.currententity;
	gl_texture_t	*texture;
	uint32_t	vert_argb = 0xFFFFFFFF;
	int		list = PVR_LIST_OP_POLY;
	int		rendermode = kRenderNormal;
	int		desired_list;
	
	if( !p ) return;
	
	if( FBitSet( p->flags, SURF_DRAWTILED ))
		GL_ResetFogColor();
	
	// Determine entity render mode -> list + blending + vertex color.
	if( RI.currententity )
		rendermode = RI.currententity->curstate.rendermode;

	// World always uses OP list
	if( RI.currententity == CL_GetEntityByIndex( 0 ))
		rendermode = kRenderNormal;

	desired_list = ( RI.currententity && rendermode != kRenderNormal ) ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY;

	// Choose texture for this surface:
	// Prefer the explicitly bound texture (supports animations via GL_Bind / R_TextureAnimation),
	// but fall back to the surface's base texture if nothing is bound.
	int texnum = glState.currentTexturesIndex;
	if( ( texnum <= 0 ) && surf && surf->texinfo && surf->texinfo->texture )
		texnum = surf->texinfo->texture->gl_texturenum;

	if( p->flags & SURF_CONVEYOR )
	{
		float		flConveyorSpeed = 0.0f;
		float		flRate, flAngle;
		
		if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ) && RI.currententity == CL_GetEntityByIndex( 0 ))
		{
			// same as doom speed
			flConveyorSpeed = -35.0f;
		}
		else
		{
			flConveyorSpeed = (e->curstate.rendercolor.g<<8|e->curstate.rendercolor.b) / 16.0f;
			if( e->curstate.rendercolor.r ) flConveyorSpeed = -flConveyorSpeed;
		}
		texture = ( texnum > 0 ) ? R_GetTexture( texnum ) : NULL;
		if( !texture )
			goto conveyor_done;
		
		flRate = fabs( flConveyorSpeed ) / (float)texture->srcWidth;
		flAngle = ( flConveyorSpeed >= 0 ) ? 180 : 0;
		
		SinCos( flAngle * ( M_PI_F / 180.0f ), &sy, &cy );
		sOffset = gp_cl->time * cy * flRate;
		tOffset = gp_cl->time * sy * flRate;
		
		// make sure that we are positive
		if( sOffset < 0.0f ) sOffset += 1.0f + -(int)sOffset;
		if( tOffset < 0.0f ) tOffset += 1.0f + -(int)tOffset;
		
		// make sure that we are in a [0,1] range
		sOffset = sOffset - (int)sOffset;
		tOffset = tOffset - (int)tOffset;
	}
	else
	{
conveyor_done:
		sOffset = tOffset = 0.0f;
	}
	
	// Get current texture (chosen above)
	pvr_ptr_t tex_addr = NULL;
	// Default fallback (only used if texture not loaded yet)
	// Actual format comes from GL_SetTextureFormat() in pvr_image.c which sets NONTWIDDLED for uncompressed textures
	uint32_t tex_format = PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED;
	int tex_width = 64, tex_height = 64;
	qboolean tex_has_mips = false;
	
	if( texnum > 0 && texnum < MAX_TEXTURES )
	{
		gl_texture_t *glt = R_GetTexture( texnum );
		if( glt && glt->loaded && glt->vram_ptr )
		{
			tex_addr = glt->vram_ptr;
			// Use format exactly as set by GL_SetTextureFormat() in pvr_image.c
			// This includes the correct NONTWIDDLED/TWIDDLED flag and base format (RGB565/ARGB1555/ARGB4444)
			tex_format = glt->format;
			tex_width = glt->width;
			tex_height = glt->height;
			tex_has_mips = ( glt->numMips > 1 ) ? true : false;
		}
	}
	
	// Default: opaque in OP list
	list = desired_list;
	
	// Use shared DR state if provided (texture chain batching), otherwise create our own (entity rendering)
	// Initialize DR state AFTER determining which list we're using (important for real hardware)
	pvr_dr_state_t dr_state_local;
	pvr_dr_state_t *dr_state_ptr = shared_dr_state;
	qboolean own_dr_state = ( shared_dr_state == NULL );
	
	if( own_dr_state )
	{
		// Initialize local DR state for entity rendering (especially important for TR list on real hardware)
		pvr_dr_init( &dr_state_local );
		dr_state_ptr = &dr_state_local;
	}
	
	// Check if we need Gouraud shading (surfaces with lightmaps)
	// Both world surfaces and brush entities can have lightmaps (surf->samples)
	// But exclude TransTexture entities - they should use flat shading
	qboolean use_gouraud = false;
	if( surf && WORLDMODEL && ( ( surf->samples && WORLDMODEL->lightdata ) ||
		( FBitSet( WORLDMODEL->flags, MODEL_LT2_LIGHTING ) && WORLDMODEL->lt2_payload && WORLDMODEL->lt2_lightsurfs )))
	{
		// Don't apply Gouraud to TransTexture 
		if( rendermode == kRenderTransTexture )
		{
			use_gouraud = false;
		}
		else
		{
			use_gouraud = true;
		}
	}

	// Entity color setup (for translucent entities)
	uint32_t vertex_colors[64] = { 0 };
	const uint32_t *vertex_colors_ptr = NULL;

	if( RI.currententity && rendermode != kRenderNormal )
	{
		// Non-opaque entity surfaces must be emitted into the TR list.
		list = PVR_LIST_TR_POLY;

		// Global alpha/color from entity state
		uint8_t a = (uint8_t)RI.currententity->curstate.renderamt;
		uint8_t r = (uint8_t)RI.currententity->curstate.rendercolor.r;
		uint8_t g = (uint8_t)RI.currententity->curstate.rendercolor.g;
		uint8_t b = (uint8_t)RI.currententity->curstate.rendercolor.b;

		// If renderamt is 0 but engine computed tr.blend (CL_FxBlend), use it.
		if( a == 0 && tr.blend > 0.0f && tr.blend < 1.0f )
			a = (uint8_t)(tr.blend * 255.0f);
		// If still 0, treat as fully opaque (common default for some HL entities).
		if( a == 0 )
			a = 255;

		// HL rendermodes:
		// - kRenderTransTexture/kRenderTransAlpha: alpha comes from renderamt/blend; RGB should not tint (use white)
		// - kRenderTransColor: tint comes from rendercolor (RGB) and alpha from renderamt
		// - additive/glow: handled below in header; we still want sane RGB defaults
		switch( rendermode )
		{
		case kRenderTransColor:
			// keep entity tint; if unset, default to white
			if( r == 0 && g == 0 && b == 0 )
				r = g = b = 255;
			break;
		case kRenderTransTexture:
		case kRenderTransAlpha:
		default:
			// no tinting
			r = g = b = 255;
			break;
		}

		// For entities, we use flat shading with a single color
		// Fill the entire array with the same color for flat shading
		uint32_t entity_color = (a << 24) | (r << 16) | (g << 8) | b;
		for( int i = 0; i < 64; i++ )
			vertex_colors[i] = entity_color;
		vertex_colors_ptr = vertex_colors; // Use flat color for entities
	}

	// Setup polygon header with texture
	pvr_poly_cxt_t cxt;
	if( tex_addr )
	{
		pvr_poly_cxt_txr(&cxt, list, tex_format,
				tex_width, tex_height, tex_addr, PVR_FILTER_BILINEAR);
		// IMPORTANT: VQ mipmapped textures have a different memory layout than non-mip VQ.
		// If we don't set mipmap mode here, the PVR will interpret the payload incorrectly
		// and even the base level will look corrupted (diagonal garbage).
		cxt.txr.mipmap = tex_has_mips ? PVR_MIPMAP_ENABLE : PVR_MIPMAP_DISABLE;
	}
	else
	{
		pvr_poly_cxt_col(&cxt, list);
	}
	cxt.gen.culling = PVR_CULLING_NONE;
	cxt.gen.fog_type = glState.isFogEnabled ? PVR_FOG_TABLE : PVR_FOG_DISABLE;
	// Use Gouraud shading for world surfaces with lightmaps (flat is default, don't set explicitly)
	if( use_gouraud )
		cxt.gen.shading = PVR_SHADE_GOURAUD;
	// For Gouraud shaded world surfaces: MODULATE so vertex colors multiply with texture
	// For entities: REPLACE (opaque) or MODULATEALPHA (translucent)
	if( use_gouraud )
		cxt.txr.env = PVR_TXRENV_MODULATE;
	else
		cxt.txr.env = (list == PVR_LIST_TR_POLY) ? PVR_TXRENV_MODULATEALPHA : PVR_TXRENV_REPLACE;

	// Depth/blend setup for entity rendermodes
	if( list == PVR_LIST_TR_POLY )
	{
		cxt.gen.alpha = PVR_ALPHA_ENABLE;
		// We submit z = 1/w, so nearer pixels have *larger* Z.
		// For translucent entities, use LEQUAL to allow rendering behind opaque geometry
		// Depth buffer was written by OP list, so translucent should test against it
		cxt.depth.comparison = PVR_DEPTHCMP_LEQUAL;
		cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		// Map HL rendermodes to PVR blending
		switch( rendermode )
		{
		case kRenderTransAdd:
		case kRenderGlow:
			// OpenGL path is effectively additive (GL_ONE, GL_ONE) with color scaled by tr.blend.
			// Use additive blend and ensure source alpha doesn't darken.
			cxt.blend.src = PVR_BLEND_ONE;
			cxt.blend.dst = PVR_BLEND_ONE;
			break;
		case kRenderTransColor:
		case kRenderTransTexture:
		case kRenderTransAlpha:
		default:
			// standard alpha blend
			cxt.blend.src = PVR_BLEND_SRCALPHA;
			cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
			break;
		}
	}
	else
	{
		cxt.gen.alpha = PVR_ALPHA_DISABLE;
		// We submit z = 1/w, so nearer pixels have *larger* Z.
		cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
		cxt.depth.write = PVR_DEPTHWRITE_ENABLE;
		// Blending is disabled by default for opaque surfaces
	}
	
	// CRITICAL: Check if we'll actually submit vertices BEFORE submitting header.
	if( !p || p->numverts < 3 )
		return;
	
	// Only submit header if we own the DR state (entity rendering). Texture chains submit header once per texture.
	if( own_dr_state )
	{
		pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target(*dr_state_ptr);
		pvr_poly_compile(hdr, &cxt);
		pvr_dr_commit(hdr);
	}
	
	// Draw polygon: use Gouraud for world surfaces with lightmaps, flat for others
	if( use_gouraud )
	{
		// Gouraud shaded - samples light per vertex from surf->samples
		DrawGLPolySurfaceGouraud( p, dr_state_ptr, surf, sOffset, tOffset, xScale, yScale );
	}
	else
	{
		// Flat shading - pass vertex colors (for entities) or NULL (for flat white)
		DrawGLPolyVertices( p, dr_state_ptr, vertex_colors_ptr, sOffset, tOffset, xScale, yScale );
	}
	
	// Only finish DR if we created our own state (entity rendering). Texture chains finish once per texture.
	if( own_dr_state )
		pvr_dr_finish();
	
	if( FBitSet( p->flags, SURF_DRAWTILED ))
		GL_SetupFogColorForSurfaces();
}
static qboolean R_HasLightmap( void )
{
    if( r_fullbright->value )
        return false;

	if( !WORLDMODEL )
		return false;

	/* LT2 keeps WORLDMODEL->lightdata NULL on purpose. */
	if( !WORLDMODEL->lightdata &&
	    !( FBitSet( WORLDMODEL->flags, MODEL_LT2_LIGHTING ) && WORLDMODEL->lt2_payload && WORLDMODEL->lt2_lightsurfs ))
		return false;

	if( RI.currententity )
	{
		if( RI.currententity->curstate.effects & EF_FULLBRIGHT )
			return false;	// disabled by user

		// check for rendermode
		switch( RI.currententity->curstate.rendermode )
		{
		case kRenderTransTexture:
		case kRenderTransColor:
		case kRenderTransAdd:
		case kRenderGlow:
			return false; // no lightmaps
		}
	}

	return true;
}

/*
================
R_BlendLightmaps
================
*/
static void R_BlendLightmaps( void )
{
	msurface_t	*surf, *newsurf = NULL;
	int		i;

	if( !R_HasLightmap() )
		return;

	GL_SetupFogColorForSurfacesEx( r_detailtextures.value ? 3 : 2, 1.0f, true );
#if 0
	if( !r_lightmap->value )
		pglEnable( GL_BLEND );
	else pglDisable( GL_BLEND );

	// lightmapped solid surfaces
	pglDepthMask( GL_FALSE );
	pglDepthFunc( GL_EQUAL );
	pglDisable( GL_ALPHA_TEST );
	if( gl_overbright.value )
	{
		pglBlendFunc( GL_DST_COLOR, GL_SRC_COLOR );
		if(!( R_HasEnabledVBO() && !r_vbo_overbrightmode.value ))
			pglColor4f( 128.0f / 192.0f, 128.0f / 192.0f, 128.0f / 192.0f, 1.0f );
	}
	else
	{
		pglBlendFunc( GL_ZERO, GL_SRC_COLOR );
	}
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

	// render static lightmaps first
	for( i = 0; i < MAX_LIGHTMAPS; i++ )
	{
		if( gl_lms.lightmap_surfaces[i] )
		{
			GL_Bind( XASH_TEXTURE0, tr.lightmapTextures[i] );

			for( surf = gl_lms.lightmap_surfaces[i]; surf != NULL; surf = surf->info->lightmapchain )
			{
				if( surf->polys ) DrawGLPolyChain( surf->polys, 0.0f, 0.0f );
			}
		}
	}

	// render dynamic lightmaps
	if( r_dynamic->value )
	{
		LM_InitBlock();
		GL_Bind( XASH_TEXTURE0, tr.dlightTexture );
		newsurf = gl_lms.dynamic_surfaces;

		for( surf = gl_lms.dynamic_surfaces; surf != NULL; surf = surf->info->lightmapchain )
		{
			int		smax, tmax;
			int		sample_size;
			mextrasurf_t	*info = surf->info;
			byte		*base;

			sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
			smax = ( info->lightextents[0] / sample_size ) + 1;
			tmax = ( info->lightextents[1] / sample_size ) + 1;

			if( LM_AllocBlock( smax, tmax, &surf->info->dlight_s, &surf->info->dlight_t ))
			{
				base = gl_lms.lightmap_buffer;
				base += ( surf->info->dlight_t * BLOCK_SIZE + surf->info->dlight_s ) * LIGHTMAP_BPP;

				R_BuildLightMap( surf, base, BLOCK_SIZE * LIGHTMAP_BPP, true );
			}
			else
			{
				msurface_t	*drawsurf;

				// upload what we have so far
				LM_UploadBlock( true );

				// draw all surfaces that use this lightmap
				for( drawsurf = newsurf; drawsurf != surf; drawsurf = drawsurf->info->lightmapchain )
				{
					if( drawsurf->polys )
					{
						DrawGLPolyChain( drawsurf->polys,
						( drawsurf->light_s - drawsurf->info->dlight_s ) * ( 1.0f / (float)BLOCK_SIZE ),
						( drawsurf->light_t - drawsurf->info->dlight_t ) * ( 1.0f / (float)BLOCK_SIZE ));
					}
				}

				newsurf = drawsurf;

				// clear the block
				LM_InitBlock();

				// try uploading the block now
				if( !LM_AllocBlock( smax, tmax, &surf->info->dlight_s, &surf->info->dlight_t ))
					gEngfuncs.Host_Error( "AllocBlock: full\n" );

				base = gl_lms.lightmap_buffer;
				base += ( surf->info->dlight_t * BLOCK_SIZE + surf->info->dlight_s ) * LIGHTMAP_BPP;

				R_BuildLightMap( surf, base, BLOCK_SIZE * LIGHTMAP_BPP, true );
			}
		}

		// draw remainder of dynamic lightmaps that haven't been uploaded yet
		if( newsurf ) LM_UploadBlock( true );

		for( surf = newsurf; surf != NULL; surf = surf->info->lightmapchain )
		{
			if( surf->polys )
			{
				DrawGLPolyChain( surf->polys,
				( surf->light_s - surf->info->dlight_s ) * ( 1.0f / (float)BLOCK_SIZE ),
				( surf->light_t - surf->info->dlight_t ) * ( 1.0f / (float)BLOCK_SIZE ));
			}
		}
	}

	pglDisable( GL_BLEND );
	pglDepthMask( GL_TRUE );
	pglDepthFunc( GL_LEQUAL );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
	pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
#endif // TODO
	// restore fog here
	GL_ResetFogColor();
}

/*
================
R_RenderFullbrights
================
*/
static void R_RenderFullbrights( void )
{
	mextrasurf_t	*es, *p;
	int		i;

	if( !R_SeparatePassActive( &draw_fullbrights ))
		return;

	R_AllowFog( false );
	R_ResetSeparatePass( &draw_fullbrights );
	R_AllowFog( true );
}

/*
================
R_RenderDetails
================
*/
static void R_RenderDetails( int passes )
{

}

static void R_RenderFullbrightForSurface( msurface_t *fa, texture_t *t )
{
	if( !t->fb_texturenum )
		return;

	fa->info->lumachain = fullbright_surfaces[t->fb_texturenum];
	fullbright_surfaces[t->fb_texturenum] = fa->info;
	R_AddToSeparatePass( &draw_fullbrights, t->fb_texturenum );
}

static void R_RenderDetailsForSurface( msurface_t *fa, texture_t *t )
{

}

static void R_RenderDecalsForSurface( msurface_t *fa, int cull_type )
{
	if( RI.currententity->curstate.rendermode == kRenderNormal )
	{
		// batch decals to draw later
		if( tr.num_draw_decals < MAX_DECAL_SURFS && fa->pdecals )
		{
			tr.draw_decals[tr.num_draw_decals].surf = fa;
			memcpy( tr.draw_decals[tr.num_draw_decals].world_matrix, r_world_matrix, sizeof( r_world_matrix ));
			tr.num_draw_decals++;
		}
	}
	else
	{
		// if rendermode != kRenderNormal draw decals sequentially
		DrawSurfaceDecals( fa, true, (cull_type == CULL_BACKSIDE));
	}
}

static qboolean R_CheckLightMap( msurface_t *fa )
{
	qboolean is_dynamic = false;
	int maps;

#if 1
	/* LT2 keeps WORLDMODEL->lightdata NULL on purpose. */
	if( !WORLDMODEL->lightdata &&
	    !( FBitSet( WORLDMODEL->flags, MODEL_LT2_LIGHTING ) && WORLDMODEL->lt2_payload && WORLDMODEL->lt2_lightsurfs ))
		return false;
#else
	// check for lightmap modification
	for( maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++ )
	{
		if( tr.lightstylevalue[fa->styles[maps]] != fa->cached_light[maps] )
			goto dynamic;
	}
#endif
	// dynamic this frame or dynamic previously
	if( fa->dlightframe == tr.framecount )
	{
 dynamic:
		// NOTE: at this point we have only valid textures
		if( r_dynamic->value )
			is_dynamic = true;
	}
#if 0
	if( is_dynamic )
	{
		const int style = fa->styles[maps];

		if( maps < MAXLIGHTMAPS && ( style >= 32 || style == 0 || style == 20 ) && fa->dlightframe != tr.framecount )
		{
			byte		temp[132*132*LIGHTMAP_BPP];
			mextrasurf_t	*info = fa->info;
			int		sample_size;
			int		smax, tmax;

			sample_size = gEngfuncs.Mod_SampleSizeForFace( fa );
			smax = ( info->lightextents[0] / sample_size ) + 1;
			tmax = ( info->lightextents[1] / sample_size ) + 1;

			if( smax < 132 && tmax < 132 )
				R_BuildLightMap( fa, temp, smax * LIGHTMAP_BPP, true );
			else
			{
				smax = Q_min( smax, 132 );
				tmax = Q_min( tmax, 132 );
				memset( temp, 255, sizeof( temp ));
				//Host_MapDesignError( "%s: bad surface extents: %d %d", __func__, fa->extents[0], fa->extents[1] );
			}

			R_SetCacheState( fa );


			GL_Bind( XASH_TEXTURE0, tr.lightmapTextures[fa->lightmaptexturenum] );

			pglTexSubImage2D( GL_TEXTURE_2D, 0, fa->light_s, fa->light_t, smax, tmax, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, temp );


		}
		else
			return true; // add to dynamic chain
	}
#endif // TODO
	return false; // updated
}

static void R_RenderLightmapForSurface( msurface_t *fa )
{
	if( !fa->polys || FBitSet( fa->flags, SURF_DRAWTILED ))
		return;

	if( R_CheckLightMap( fa ))
	{
		fa->info->lightmapchain = gl_lms.dynamic_surfaces;
		gl_lms.dynamic_surfaces = fa;
	}
	else
	{
		fa->info->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
		gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
	}
}

/*
================
R_RenderBrushPoly
================
*/
static void R_RenderBrushPoly( msurface_t *fa, int cull_type )
{
	texture_t	*t;

	r_stats.c_world_polys++;

	if( fa->flags & SURF_DRAWSKY )
		return; // already handled

	t = R_TextureAnimation( fa );

	if( FBitSet( fa->flags, SURF_DRAWTURB ))
	{
		// Water/warp surface, no lightmaps.
		// If wateralpha < 1, render later in TR pass; otherwise render now in OP list.
		if( tr.movevars->wateralpha < 1.0f )
		{
			// keep existing separate-pass bookkeeping
			// (draw_wateralpha is managed in R_DrawTextureChains).
			return;
		}

		// Bind ripple texture (or base texture) before emitting polys.
		qboolean use_ripples = R_UploadRipples( t );
		EmitWaterPolys( fa, cull_type == CULL_BACKSIDE, use_ripples );
		return;
	}
	else GL_Bind( XASH_TEXTURE0, t->gl_texturenum );

	// Draw all polys in the chain (surfaces can be subdivided into multiple polys)
	// Pass NULL for shared_dr_state so DrawGLPoly creates its own (for entity rendering outside texture chains)
	for( glpoly2_t *p = fa->polys; p != NULL; p = p->chain )
		DrawGLPoly( p, 0.0f, 0.0f, fa, NULL );
	R_RenderDecalsForSurface( fa, cull_type );
}

/*
================
R_DrawTextureChains
================
*/
static void R_DrawTextureChains( void )
{
	int		i;
	msurface_t	*s;
	texture_t		*t;
	// reset lightmap chains for this pass (lightmap blending pass is still TODO,
	// but we keep chains consistent with gl_rsurf.c logic).
	memset( gl_lms.lightmap_surfaces, 0, sizeof( gl_lms.lightmap_surfaces ));
	gl_lms.dynamic_surfaces = NULL;

	// restore worldmodel
	RI.currententity = CL_GetEntityByIndex( 0 );
	RI.currentmodel = RI.currententity ? RI.currententity->model : NULL;

	// make sure color is reset
	glState.currentColor = 0xFFFFFFFF;

	// Skybox/clouds path is still TODO for PVR; we still build the skybox clip chain.
	for( s = skychain; s != NULL; s = s->texturechain )
		R_AddSkyBoxSurface( s );

	for( i = 0; i < WORLDMODEL->numtextures; i++ )
	{
		t = WORLDMODEL->textures[i];
		if( !t ) continue;

		s = t->texturechain;

		if( !s || ( i == tr.skytexturenum ))
			continue;

		// Keep original separate-pass bookkeeping (even if passes are not yet drawn).
		if(( s->flags & SURF_DRAWTURB ) && tr.movevars->wateralpha < 1.0f )
		{
			R_AddToSeparatePass( &draw_wateralpha, i );
			continue;
		}

		if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ) && FBitSet( s->flags, SURF_TRANSPARENT ))
		{
			R_AddToSeparatePass( &draw_alpha_surfaces, i );
			continue;
		}

		// Initialize DR state once per texture (Quake2 pattern: init once, submit header once, all polys share it, finish once)
		pvr_dr_state_t dr_state;
#if REF_PVR_PROFILE
		PVR_Prof_Start();
#endif
		pvr_dr_init( &dr_state );

		// Real HW safety: never submit a poly header unless we are sure we will emit at least one vertex afterward.
		// If a header is submitted and all polys get clipped/culled, the TA can mis-parse the next header as a vertex,
		// which manifests as gray screen + tiny quad and then no frames.
		qboolean will_emit_any = false;
		for( msurface_t *s2 = s; s2 != NULL && !will_emit_any; s2 = s2->texturechain )
		{
			for( glpoly2_t *p2 = s2->polys; p2 != NULL; p2 = p2->chain )
			{
				if( DrawGLPoly_AnyVertexVisible( p2 ))
				{
					will_emit_any = true;
					break;
				}
			}
		}
		if( !will_emit_any )
		{
			t->texturechain = NULL;
			continue;
		}
		
		// Submit header once per texture (all surfaces with this texture share it)
		// Get texture info for header
		int texnum = t->gl_texturenum;
		pvr_ptr_t tex_addr = NULL;
		uint32_t tex_format = PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED;
		int tex_width = 64, tex_height = 64;
		qboolean tex_has_mips = false;
		
		if( texnum > 0 && texnum < MAX_TEXTURES )
		{
			gl_texture_t *glt = R_GetTexture( texnum );
			if( glt && glt->loaded && glt->vram_ptr )
			{
				tex_addr = glt->vram_ptr;
				tex_format = glt->format;
				tex_width = glt->width;
				tex_height = glt->height;
				tex_has_mips = ( glt->numMips > 1 ) ? true : false;
			}
		}
		
		pvr_poly_cxt_t cxt;
		if( tex_addr )
		{
			pvr_poly_cxt_txr( &cxt, PVR_LIST_OP_POLY, tex_format, tex_width, tex_height, tex_addr, PVR_FILTER_BILINEAR );
			// See note above: must enable mipmap mode for mipmapped payloads (VQ mipmaps in particular).
			cxt.txr.mipmap = tex_has_mips ? PVR_MIPMAP_ENABLE : PVR_MIPMAP_DISABLE;
		}
		else
		{
			pvr_poly_cxt_col( &cxt, PVR_LIST_OP_POLY );
		}
		cxt.gen.culling = PVR_CULLING_NONE;
		cxt.gen.fog_type = glState.isFogEnabled ? PVR_FOG_TABLE : PVR_FOG_DISABLE;
		// World base pass: we use per-vertex light sampled from surf->samples.
		// IMPORTANT: header is submitted ONCE per texture chain, so it MUST be Gouraud+Modulate,
		// otherwise PVR will ignore vertex colors and you'll see fullbright world.
		// DrawGLPoly will call DrawGLPolySurfaceGouraud for surfaces with lightmaps.
		const qboolean use_world_vertex_light = ( !r_fullbright->value && WORLDMODEL &&
			( WORLDMODEL->lightdata || ( FBitSet( WORLDMODEL->flags, MODEL_LT2_LIGHTING ) && WORLDMODEL->lt2_payload && WORLDMODEL->lt2_lightsurfs )));
		if( use_world_vertex_light )
			cxt.gen.shading = PVR_SHADE_GOURAUD;
		cxt.txr.env = use_world_vertex_light ? PVR_TXRENV_MODULATE : PVR_TXRENV_REPLACE;
		cxt.gen.alpha = PVR_ALPHA_DISABLE;
		cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
		cxt.depth.write = PVR_DEPTHWRITE_ENABLE;
		// Blending is disabled by default for opaque surfaces
		
		pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target( dr_state );
		pvr_poly_compile( hdr, &cxt );
		pvr_dr_commit( hdr );
#if REF_PVR_PROFILE
		r_stats.t_world_setup += PVR_Prof_End();
#endif
		
		// Now render all surfaces with this texture (they share the header and DR state we just created)
		for( ; s != NULL; s = s->texturechain )
		{
#if XASH_DREAMCAST
			// Prefetch next surface's lmvecs so it's hot when we advance
			if( s->texturechain && s->texturechain->info )
				SHZ_PREFETCH( s->texturechain->info->lmvecs );
#endif
			// Bind texture for DrawGLPoly (it reads glState.currentTexturesIndex as fallback)
			GL_Bind( XASH_TEXTURE0, texnum );
			
			for( glpoly2_t *p = s->polys; p != NULL; p = p->chain )
			{
				DrawGLPoly( p, 0.0f, 0.0f, s, &dr_state );
				R_RenderDecalsForSurface( s, CULL_VISIBLE );
			}
		}

		pvr_dr_finish();
		
		t->texturechain = NULL;
	}
}

/*
================
R_DrawAlphaTextureChains
================
*/
void R_DrawAlphaTextureChains( void )
{
	int		i;
	msurface_t	*s;
	texture_t		*t;
	pvr_dr_state_t	dr_state;

	if( !R_SeparatePassActive( &draw_alpha_surfaces ))
		return;

	// restore worldmodel
	RI.currententity = CL_GetEntityByIndex( 0 );
	RI.currentmodel = RI.currententity->model;

	GL_SetupFogColorForSurfaces();

	pvr_dr_init( &dr_state );

	for( i = draw_alpha_surfaces.first; i <= draw_alpha_surfaces.last; i++ )
	{
		t = WORLDMODEL->textures[i];
		if( !t )
			continue;

		s = t->texturechain;

		if( !s || !FBitSet( s->flags, SURF_TRANSPARENT ))
			continue;

		// Get texture for this surface
		texture_t *tex = R_TextureAnimation( s );
		if( !tex )
			continue;
		int texnum = tex->gl_texturenum;
		gl_texture_t *glt = R_GetTexture( texnum );
		if( !glt || !glt->loaded || !glt->vram_ptr )
			continue;

		// Setup PVR context for alpha-tested (punch-through) rendering in PT list.
		// This matches the Quake path (alpha-test, no blending) and writes depth.
		pvr_poly_cxt_t cxt;
		pvr_poly_cxt_txr( &cxt, PVR_LIST_PT_POLY, glt->format, glt->width, glt->height, glt->vram_ptr, PVR_FILTER_BILINEAR );
		cxt.gen.culling = PVR_CULLING_NONE;
		cxt.gen.fog_type = glState.isFogEnabled ? PVR_FOG_TABLE : PVR_FOG_DISABLE;
		cxt.gen.alpha = PVR_ALPHA_ENABLE;
		// Flat shading is default, don't set explicitly
		cxt.txr.env = PVR_TXRENV_REPLACE;

		// Depth testing: same as opaque, but allow equal. Depth write must be enabled
		// so PT pixels participate in later TR depth=EQUAL passes (lightmaps).
		cxt.depth.comparison = PVR_DEPTHCMP_LEQUAL;
		cxt.depth.write = PVR_DEPTHWRITE_ENABLE;
		// Blending is disabled by default for opaque surfaces

		// Submit header for this texture
		pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target( dr_state );
		pvr_poly_compile( hdr, &cxt );
		pvr_dr_commit( hdr );

		// Render all surfaces with this texture
		for( ; s != NULL; s = s->texturechain )
		{
			if( !FBitSet( s->flags, SURF_TRANSPARENT ))
				continue;

			// Draw base texture into PT list using the already-submitted header.
			// IMPORTANT: do NOT call R_RenderBrushPoly here because DrawGLPoly currently
			// hardcodes OP list headers (would be ignored in PT).
			if( s->polys )
				DrawGLPolyVertices( s->polys, &dr_state, NULL, 0.0f, 0.0f, 0.0f, 0.0f );

		}

		t->texturechain = NULL;
	}

	pvr_dr_finish();

	R_ResetSeparatePass( &draw_alpha_surfaces );

	GL_ResetFogColor();
}

/*
================
R_DrawWaterSurfaces
================
*/
void R_DrawWaterSurfaces( void )
{
	int		i;
	msurface_t	*s;
	texture_t		*t;

	if( !RI.drawWorld || RI.onlyClientDraw )
		return;

	// non-transparent water is already drawed
	if( !R_SeparatePassActive( &draw_wateralpha ))
		return;

	// restore worldmodel
	RI.currententity = CL_GetEntityByIndex( 0 );
	RI.currentmodel = RI.currententity->model;

	// go back to the world matrix
	R_LoadIdentity();

	for( i = draw_wateralpha.first; i <= draw_wateralpha.last; i++ )
	{
		t = WORLDMODEL->textures[i];
		if( !t ) continue;

		s = t->texturechain;
		if( !s ) continue;

		// Check if first surface is water (like GL code does)
		if( !FBitSet( s->flags, SURF_DRAWTURB ))
			continue;

		// Render all water surfaces with this texture (like GL code)
		// Note: GL code doesn't check SURF_DRAWTURB again in the loop, it just calls EmitWaterPolys for all
		for( ; s; s = s->texturechain )
		{
			// Bind ripple texture (or base texture) before emitting polys.
			qboolean use_ripples = R_UploadRipples( t );
			EmitWaterPolys( s, false, use_ripples );
		}

		t->texturechain = NULL;
	}

	R_ResetSeparatePass( &draw_wateralpha );
}

/*
=================
R_SurfaceCompare

compare translucent surfaces
=================
*/
static int R_SurfaceCompare( const void *a, const void *b )
{
	msurface_t	*surf1, *surf2;
	vec3_t		org1, org2;
	float		len1, len2;

	surf1 = (msurface_t *)((sortedface_t *)a)->surf;
	surf2 = (msurface_t *)((sortedface_t *)b)->surf;

	VectorAdd( RI.currententity->origin, surf1->info->origin, org1 );
	VectorAdd( RI.currententity->origin, surf2->info->origin, org2 );

	// compare by plane dists
	len1 = DotProduct( org1, RI.vforward ) - RI.viewplanedist;
	len2 = DotProduct( org2, RI.vforward ) - RI.viewplanedist;

	if( len1 > len2 )
		return -1;
	if( len1 < len2 )
		return 1;

	return 0;
}

static void R_SetRenderMode( cl_entity_t *e )
{
#if 0
	switch( e->curstate.rendermode )
	{
	case kRenderNormal:
		pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
		break;
	case kRenderTransColor:
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		glState.currentColor = (e->curstate.renderamt << 24) | (e->curstate.rendercolor.r << 16) | (e->curstate.rendercolor.g << 8) | e->curstate.rendercolor.b;
		pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		pglDisable( GL_TEXTURE_2D );
		pglEnable( GL_BLEND );
		break;
	case kRenderTransAdd:
		pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		pglColor4f( tr.blend, tr.blend, tr.blend, 1.0f );
		pglBlendFunc( GL_ONE, GL_ONE );
		pglDepthMask( GL_FALSE );
		pglEnable( GL_BLEND );
		break;
	case kRenderTransAlpha:
		pglEnable( GL_ALPHA_TEST );
		pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ))
		{
			pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
			pglColor4f( 1.0f, 1.0f, 1.0f, tr.blend );
			pglEnable( GL_BLEND );
		}
		else
		{
			pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
			pglDisable( GL_BLEND );
		}
		pglAlphaFunc( GL_GREATER, 0.25f );
		break;
	default:
		pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		pglColor4f( 1.0f, 1.0f, 1.0f, tr.blend );
		pglDepthMask( GL_FALSE );
		pglEnable( GL_BLEND );
		break;
	}
#endif // TODO
}

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel( cl_entity_t *e )
{
	int		i, k, num_sorted;
	vec3_t		origin_l, oldorigin;
	int		old_rendermode;
	vec3_t		mins, maxs;
	int		cull_type;
	msurface_t	*psurf;
	model_t		*clmodel;
	qboolean		rotated;
	dlight_t		*l;

	if( !RI.drawWorld ) return;

	clmodel = e->model;

	if( !VectorIsNull( e->angles ))
	{
		for( i = 0; i < 3; i++ )
		{
			mins[i] = e->origin[i] - clmodel->radius;
			maxs[i] = e->origin[i] + clmodel->radius;
		}
		rotated = true;
	}
	else
	{
		VectorAdd( e->origin, clmodel->mins, mins );
		VectorAdd( e->origin, clmodel->maxs, maxs );
		rotated = false;
	}

	if( R_CullBox( mins, maxs ))
		return;

	// Brush entities must not clobber the world lightmap chain, because world lightmaps
	// are emitted later in the frame (TR pass). Save/restore around entity rendering.
	msurface_t *saved_lm[MAX_LIGHTMAPS];
	msurface_t *saved_dyn = gl_lms.dynamic_surfaces;
	memcpy( saved_lm, gl_lms.lightmap_surfaces, sizeof( saved_lm ));
	memset( gl_lms.lightmap_surfaces, 0, sizeof( gl_lms.lightmap_surfaces ));
	old_rendermode = e->curstate.rendermode;
	gl_lms.dynamic_surfaces = NULL;

	if( rotated ) R_RotateForEntity( e );
	else R_TranslateForEntity( e );

	if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ) && FBitSet( clmodel->flags, MODEL_TRANSPARENT ))
		e->curstate.rendermode = kRenderTransAlpha;

	e->visframe = tr.realframecount; // visible

	if( rotated ) PVR_Mat4x4_VectorITransform( &RI.objectMatrix, RI.cullorigin, tr.modelorg );
	else VectorSubtract( RI.cullorigin, e->origin, tr.modelorg );

	// calculate dynamic lighting for bmodel
	for( k = 0; k < MAX_DLIGHTS; k++ )
	{
		l = &tr.dlights[k];

		if( l->die < gp_cl->time || !l->radius )
			continue;

		VectorCopy( l->origin, oldorigin ); // save lightorigin
		PVR_Mat4x4_VectorITransform( &RI.objectMatrix, l->origin, origin_l );
		VectorCopy( origin_l, l->origin ); // move light in bmodel space
		R_MarkLights( l, 1<<k, clmodel->nodes + clmodel->hulls[0].firstclipnode );
		VectorCopy( oldorigin, l->origin ); // restore lightorigin
	}

	// setup the rendermode
	R_SetRenderMode( e );
	if( e->curstate.rendermode == kRenderTransAdd )
	{
		R_AllowFog( false );
	}

	GL_SetupFogColorForSurfaces ();

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

	num_sorted = 0;

	for( i = 0; i < clmodel->nummodelsurfaces; i++, psurf++ )
	{
		if( FBitSet( psurf->flags, SURF_DRAWTURB ) && !ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ))
		{
			if( psurf->plane->type != PLANE_Z && !FBitSet( e->curstate.effects, EF_WATERSIDES ))
				continue;
			if( mins[2] + 1.0f >= psurf->plane->dist )
				continue;
		}

		cull_type = R_CullSurface( psurf, &RI.frustum, RI.frustum.clipFlags );

		if( cull_type >= CULL_FRUSTUM )
			continue;

		if( cull_type == CULL_BACKSIDE )
		{
			if( !FBitSet( psurf->flags, SURF_DRAWTURB ) && !( psurf->pdecals && e->curstate.rendermode == kRenderTransTexture ))
				continue;
		}

		if( num_sorted < gpGlobals->max_surfaces )
		{
			gpGlobals->draw_surfaces[num_sorted].surf = psurf;
			gpGlobals->draw_surfaces[num_sorted].cull = cull_type;
			num_sorted++;
		}
	}

	// sort faces if needs
	if( !FBitSet( clmodel->flags, MODEL_LIQUID ) && e->curstate.rendermode == kRenderTransTexture && !gl_nosort.value )
		qsort( gpGlobals->draw_surfaces, num_sorted, sizeof( sortedface_t ), R_SurfaceCompare );


	// draw sorted translucent surfaces
	for( i = 0; i < num_sorted; i++ )
		R_RenderBrushPoly( gpGlobals->draw_surfaces[i].surf, gpGlobals->draw_surfaces[i].cull );

	GL_ResetFogColor();
	//DrawDecalsBatch();

	// restore fog here
	if( e->curstate.rendermode == kRenderTransAdd )
		R_AllowFog( true );

	e->curstate.rendermode = old_rendermode;
#if 0
	pglDisable( GL_ALPHA_TEST );
	pglAlphaFunc( GL_GREATER, DEFAULT_ALPHATEST );
	pglDisable( GL_BLEND );
	pglDepthMask( GL_TRUE );
#endif // TODO

	// restore world lightmap chains/state and matrix
	memcpy( gl_lms.lightmap_surfaces, saved_lm, sizeof( saved_lm ));
	gl_lms.dynamic_surfaces = saved_dyn;
	R_LoadIdentity();	// restore worldmatrix
}

/*
=============================================================

	WORLD MODEL

=============================================================
*/

// Fast plane distance using SH4Zam for non-axial planes.
// Mirrors the Quake2-style optimization: axial planes avoid a full dot product.
SHZ_FORCE_INLINE float R_PlaneDiff_SHZ( const vec3_t org, const mplane_t *p )
{
	switch( p->type )
	{
	case PLANE_X: return org[0] - p->dist;
	case PLANE_Y: return org[1] - p->dist;
	case PLANE_Z: return org[2] - p->dist;
	default:
	{
		// Use SH4 FIPR-based dot for best throughput.
		const shz_vec3_t v = shz_vec3_init( org[0], org[1], org[2] );
		const shz_vec3_t n = shz_vec3_init( p->normal[0], p->normal[1], p->normal[2] );
		return shz_vec3_dot( v, n ) - p->dist;
	}
	}
}

/*
================
R_RecursiveWorldNode
================
*/
static void R_RecursiveWorldNode( mnode_t *node, uint clipflags )
{
	int		i, clipped;
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	int		c, side;
	float		dot;
	mnode_t *children[2];
	int numsurfaces, firstsurface;

loc0:
	if( SHZ_UNLIKELY( node->contents == CONTENTS_SOLID ))
		return; // hit a solid leaf

	if( SHZ_UNLIKELY( node->visframe != tr.visframecount ))
		return;

	if( SHZ_UNLIKELY( clipflags && !r_nocull.value ))
	{
		const mplane_t *frustum = RI.frustum.planes;
		for( i = 0; i < 6; i++ )
		{
			const mplane_t *p = &frustum[i];

			if( !FBitSet( clipflags, BIT( i )))
				continue;

			clipped = BoxOnPlaneSide( node->minmaxs, node->minmaxs + 3, p );
			if( clipped == 2 ) return;
			if( clipped == 1 ) ClearBits( clipflags, BIT( i ));
		}
	}

	// if a leaf node, draw stuff
	if( node->contents < 0 )
	{
		pleaf = (mleaf_t *)node;

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if( c )
		{
			SHZ_PREFETCH( mark );
			do
			{
				SHZ_PREFETCH( mark + 1 );
				(*mark)->visframe = tr.framecount;
				mark++;
			} while( --c );
		}

		// deal with model fragments in this leaf
		if( pleaf->efrags )
			gEngfuncs.R_StoreEfrags( &pleaf->efrags, tr.realframecount );

		r_stats.c_world_leafs++;
		return;
	}

	// node is just a decision point, so go down the apropriate sides

	// find which side of the node we are on
	dot = R_PlaneDiff_SHZ( tr.modelorg, node->plane );
	side = (dot >= 0.0f) ? 0 : 1;

	// recurse down the children, front side first
	node_children( children, node, WORLDMODEL );
	SHZ_PREFETCH( children[!side] );
	R_RecursiveWorldNode( children[side], clipflags );

	firstsurface = node_firstsurface( node, WORLDMODEL );
	numsurfaces = node_numsurfaces( node, WORLDMODEL );

	// draw stuff
	surf = WORLDMODEL->surfaces + firstsurface;
	if( numsurfaces ) SHZ_PREFETCH( surf );
	for( c = numsurfaces; c; c--, surf++ )
	{
		SHZ_PREFETCH( surf + 1 );
		if( R_CullSurface( surf, &RI.frustum, clipflags ))
			continue;

		if( surf->flags & SURF_DRAWSKY )
		{
			// make sky chain to right clip the skybox
			surf->texturechain = skychain;
			skychain = surf;
		}
		else
		{
			surf->texturechain = surf->texinfo->texture->texturechain;
			surf->texinfo->texture->texturechain = surf;
		}
	}

	// recurse down the back side
	node = children[!side];
	goto loc0;
}

/*
================
R_CullNodeTopView

cull node by user rectangle (simple scissor)
================
*/
static qboolean R_CullNodeTopView( mnode_t *node )
{
	vec2_t	delta, size;
	vec3_t	center, half;

	// build the node center and half-diagonal
	VectorAverage( node->minmaxs, node->minmaxs + 3, center );
	VectorSubtract( node->minmaxs + 3, center, half );

	// cull against the screen frustum or the appropriate area's frustum.
	Vector2Subtract( center, world_orthocenter, delta );
	Vector2Add( half, world_orthohalf, size );

	return ( fabs( delta[0] ) > size[0] ) || ( fabs( delta[1] ) > size[1] );
}

/*
================
R_DrawTopViewLeaf
================
*/
static void R_DrawTopViewLeaf( mleaf_t *pleaf, uint clipflags )
{
	msurface_t	**mark, *surf;
	int		i;

	for( i = 0, mark = pleaf->firstmarksurface; i < pleaf->nummarksurfaces; i++, mark++ )
	{
		surf = *mark;

		// don't process the same surface twice
		if( surf->visframe == tr.framecount )
			continue;

		surf->visframe = tr.framecount;

		if( R_CullSurface( surf, &RI.frustum, clipflags ))
			continue;

		if(!( surf->flags & SURF_DRAWSKY ))
		{
			surf->texturechain = surf->texinfo->texture->texturechain;
			surf->texinfo->texture->texturechain = surf;
		}
	}

	// deal with model fragments in this leaf
	if( pleaf->efrags )
		gEngfuncs.R_StoreEfrags( &pleaf->efrags, tr.realframecount );

	r_stats.c_world_leafs++;
}

/*
================
R_DrawWorldTopView
================
*/
static void R_DrawWorldTopView( mnode_t *node, uint clipflags )
{
	int		i, c, clipped;
	msurface_t	*surf;

	do
	{
		mnode_t *children[2];
		int numsurfaces, firstsurface;

		if( node->contents == CONTENTS_SOLID )
			return;	// hit a solid leaf

		if( node->visframe != tr.visframecount )
			return;

		if( clipflags && !r_nocull.value )
		{
			for( i = 0; i < 6; i++ )
			{
				const mplane_t	*p = &RI.frustum.planes[i];

				if( !FBitSet( clipflags, BIT( i )))
					continue;

				clipped = BoxOnPlaneSide( node->minmaxs, node->minmaxs + 3, p );
				if( clipped == 2 ) return;
				if( clipped == 1 ) ClearBits( clipflags, BIT( i ));
			}
		}

		// cull against the screen frustum or the appropriate area's frustum.
		if( R_CullNodeTopView( node ))
			return;

		// if a leaf node, draw stuff
		if( node->contents < 0 )
		{
			R_DrawTopViewLeaf( (mleaf_t *)node, clipflags );
			return;
		}

		// draw stuff
		numsurfaces = node_numsurfaces( node, WORLDMODEL );
		firstsurface = node_firstsurface( node, WORLDMODEL );

		for( c = numsurfaces, surf = WORLDMODEL->surfaces + firstsurface; c; c--, surf++ )
		{
			// don't process the same surface twice
			if( surf->visframe == tr.framecount )
				continue;

			surf->visframe = tr.framecount;

			if( R_CullSurface( surf, &RI.frustum, clipflags ))
				continue;

			if(!( surf->flags & SURF_DRAWSKY ))
			{
				surf->texturechain = surf->texinfo->texture->texturechain;
				surf->texinfo->texture->texturechain = surf;
			}
		}

		// recurse down both children, we don't care the order...
		node_children( children, node, WORLDMODEL );
		R_DrawWorldTopView( children[0], clipflags );
		node = children[1];
	} while( node );
}

/*
=============
R_DrawTriangleOutlines
=============
*/
static void R_DrawTriangleOutlines( void )
{

}

/*
=============
R_DrawWorld
=============
*/
void R_DrawWorld( void )
{
	double	start, end;

	// paranoia issues: when gl_renderer is "0" we need have something valid for currententity
	// to prevent crashing until HeadShield drawing.
	RI.currententity = CL_GetEntityByIndex( 0 );
	if( !RI.currententity )
		return;

	RI.currentmodel = RI.currententity->model;
	if( !RI.drawWorld || RI.onlyClientDraw )
		return;

	VectorCopy( RI.cullorigin, tr.modelorg );
	memset( gl_lms.lightmap_surfaces, 0, sizeof( gl_lms.lightmap_surfaces ));
	memset( fullbright_surfaces, 0, sizeof( fullbright_surfaces ));
	memset( detail_surfaces, 0, sizeof( detail_surfaces ));

	gl_lms.dynamic_surfaces = NULL;
	tr.blend = 1.0f;

	R_ClearSkyBox ();

	start = gEngfuncs.pfnTime();
#if REF_PVR_PROFILE
	PVR_Prof_Start();
#endif
	if( RI.drawOrtho )
		R_DrawWorldTopView( WORLDMODEL->nodes, RI.frustum.clipFlags );
	else R_RecursiveWorldNode( WORLDMODEL->nodes, RI.frustum.clipFlags );
#if REF_PVR_PROFILE
	r_stats.t_world_node = PVR_Prof_End();
#else
	end = gEngfuncs.pfnTime();
	r_stats.t_world_node = end - start;
#endif

	start = gEngfuncs.pfnTime();

	R_DrawTextureChains();

	if( !ENGINE_GET_PARM( PARM_DEV_OVERVIEW ))
	{
		GL_ResetFogColor();
	//	DrawDecalsBatch();
		if( skychain )
		{
			// Ensure world matrix is set to viewproj for skybox (camera-relative)
			R_LoadIdentity();
			R_DrawSkyBox();
		}
	}

	end = gEngfuncs.pfnTime();

	r_stats.t_world_draw = end - start;
	skychain = NULL;

	R_DrawTriangleOutlines ();

	gEngfuncs.R_DrawWorldHull();
}

/*
===============
R_MarkLeaves

Mark the leaves and nodes that are in the PVS for the current leaf
===============
*/
void R_MarkLeaves( void )
{
	qboolean	novis = false;
	qboolean	force = false;
	mleaf_t	*leaf = NULL;
	mnode_t	*node;
	vec3_t	test;
	int	i;

	if( !RI.drawWorld ) return;

	if( FBitSet( r_novis.flags, FCVAR_CHANGED ) || tr.fResetVis )
	{
		// force recalc viewleaf
		ClearBits( r_novis.flags, FCVAR_CHANGED );
		tr.fResetVis = false;
		RI.viewleaf = NULL;
	}

	VectorCopy( RI.pvsorigin, test );

	if( RI.viewleaf != NULL )
	{
		// merge two leafs that can be a crossed-line contents
		if( RI.viewleaf->contents == CONTENTS_EMPTY )
		{
			VectorSet( test, RI.pvsorigin[0], RI.pvsorigin[1], RI.pvsorigin[2] - 16.0f );
			leaf = gEngfuncs.Mod_PointInLeaf( test, WORLDMODEL->nodes );
		}
		else
		{
			VectorSet( test, RI.pvsorigin[0], RI.pvsorigin[1], RI.pvsorigin[2] + 16.0f );
			leaf = gEngfuncs.Mod_PointInLeaf( test, WORLDMODEL->nodes );
		}

		if(( leaf->contents != CONTENTS_SOLID ) && ( RI.viewleaf != leaf ))
			force = true;
	}

	if( RI.viewleaf == RI.oldviewleaf && RI.viewleaf != NULL && !force )
		return;

	// development aid to let you run around
	// and see exactly where the pvs ends
	if( r_lockpvs.value ) return;

	RI.oldviewleaf = RI.viewleaf;
	tr.visframecount++;

	if( r_novis.value || RI.drawOrtho || !RI.viewleaf || !WORLDMODEL->visdata )
		novis = true;

	gEngfuncs.R_FatPVS( RI.pvsorigin, REFPVS_RADIUS, RI.visbytes, FBitSet( RI.params, RP_OLDVIEWLEAF ), novis );
	if( force && !novis ) gEngfuncs.R_FatPVS( test, REFPVS_RADIUS, RI.visbytes, true, novis );

	for( i = 0; i < WORLDMODEL->numleafs; i++ )
	{
		if( CHECKVISBIT( RI.visbytes, i ))
		{
			node = (mnode_t *)&WORLDMODEL->leafs[i+1];
			do
			{
				if( node->visframe == tr.visframecount )
					break;
				node->visframe = tr.visframecount;
				node = node->parent;
			} while( node );
		}
	}
}

/*
========================
GL_CreateSurfaceLightmap
========================
*/
static void GL_CreateSurfaceLightmap( msurface_t *surf, model_t *loadmodel )
{
	int		smax, tmax;
	int		sample_size;
	mextrasurf_t	*info = surf->info;
	byte		*base;

	if( !loadmodel->lightdata && !FBitSet( WORLDMODEL->flags, MODEL_LT2_LIGHTING ))
		return;

	if( FBitSet( surf->flags, SURF_DRAWTILED ))
		return;

	sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
	smax = ( info->lightextents[0] / sample_size ) + 1;
	tmax = ( info->lightextents[1] / sample_size ) + 1;

	if( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ))
	{
		LM_UploadBlock( false );
		LM_InitBlock();

		if( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ))
			gEngfuncs.Host_Error( "%s: full\n", __func__ );
	}

	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	base = gl_lms.lightmap_buffer;
	base += ( surf->light_t * BLOCK_SIZE + surf->light_s ) * LIGHTMAP_BPP;

	R_SetCacheState( surf );
	R_BuildLightMap( surf, base, BLOCK_SIZE * LIGHTMAP_BPP, false );
}

/*
==================
GL_RebuildLightmaps

Rebuilds the lightmap texture
when gamma is changed
==================
*/
void GL_RebuildLightmaps( void )
{
	int	i, j;
	model_t	*m;

	if( !ENGINE_GET_PARM( PARM_CLIENT_ACTIVE ) )
		return; // wait for worldmodel

	// release old lightmaps
	for( i = 0; i < MAX_LIGHTMAPS; i++ )
	{
		if( !tr.lightmapTextures[i] ) break;
		GL_FreeTexture( tr.lightmapTextures[i] );
	}

	memset( tr.lightmapTextures, 0, sizeof( tr.lightmapTextures ));
	gl_lms.current_lightmap_texture = 0;

	// setup all the lightstyles
	CL_RunLightStyles((lightstyle_t *)ENGINE_GET_PARM( PARM_GET_LIGHTSTYLES_PTR ));

	LM_InitBlock();

	for( i = 0; i < gp_cl->nummodels; i++ )
	{
		if(( m = CL_ModelHandle( i + 1 )) == NULL )
			continue;

		if( m->name[0] == '*' || m->type != mod_brush )
			continue;

		for( j = 0; j < m->numsurfaces; j++ )
			GL_CreateSurfaceLightmap( m->surfaces + j, m );
	}
	LM_UploadBlock( false );

	if( gEngfuncs.drawFuncs->GL_BuildLightmaps )
	{
		// build lightmaps on the client-side
		gEngfuncs.drawFuncs->GL_BuildLightmaps( );
	}
}


/*
==================
GL_BuildLightmaps

Builds the lightmap texture
with all the surfaces from all brush models
==================
*/
void GL_BuildLightmaps( void )
{
	int	i, j, nColinElim = 0;
	model_t	*m;
	
	// PVR uses vertex lighting with gouraud shading, not lightmap textures
	// We still need to set up light samples for vertex lighting, but don't build textures
	
	memset( &RI, 0, sizeof( RI ));

	tr.block_size = BLOCK_SIZE_DEFAULT;
	skychain = NULL;

	tr.framecount = tr.visframecount = 1;	// no dlight cache
	gl_lms.current_lightmap_texture = 0;
	tr.modelviewIdentity = false;
	tr.realframecount = 1;

	// setup the texture for dlights
	R_InitDlightTexture();

	// setup all the lightstyles
	CL_RunLightStyles((lightstyle_t *)ENGINE_GET_PARM( PARM_GET_LIGHTSTYLES_PTR ));

    LM_InitBlock();

    for( i = 0; i < gp_cl->nummodels; i++ )
    {
        if(( m = CL_ModelHandle( i + 1 )) == NULL )
            continue;

        if( m->name[0] == '*' || m->type != mod_brush )
            continue;

        for( j = 0; j < m->numsurfaces; j++ )
        {
            // clearing all decal chains
            m->surfaces[j].pdecals = NULL;
            m->surfaces[j].visframe = 0;

            GL_CreateSurfaceLightmap( m->surfaces + j, m );

            if( m->surfaces[j].flags & SURF_DRAWTURB )
                continue;

            nColinElim += GL_BuildPolygonFromSurface( m, m->surfaces + j );
        }

        // clearing visframe
        for( j = 0; j < m->numleafs; j++ )
            m->leafs[j+1].visframe = 0;
        for( j = 0; j < m->numnodes; j++ )
            m->nodes[j].visframe = 0;
    }

    // Don't upload lightmap block - we use vertex lighting instead
    // LM_UploadBlock( false );

	if( gEngfuncs.drawFuncs->GL_BuildLightmaps )
	{
		// build lightmaps on the client-side
		gEngfuncs.drawFuncs->GL_BuildLightmaps( );
	}
}

void GL_InitRandomTable( void )
{
	int	tu, tv;

	for( tu = 0; tu < MOD_FRAMES; tu++ )
	{
		for( tv = 0; tv < MOD_FRAMES; tv++ )
		{
			rtable[tu][tv] = gEngfuncs.COM_RandomLong( 0, 0x7FFF );
		}
	}

	gEngfuncs.COM_SetRandomSeed( 0 );
}

#if XASH_DREAMCAST
uintptr_t R_SurfGetSampleVertexLightAddr( void )
{
	return (uintptr_t)&SampleVertexLight;
}

uintptr_t R_SurfGetDrawGLPolyVerticesAddr( void )
{
	return (uintptr_t)&DrawGLPolyVertices;
}
#endif
