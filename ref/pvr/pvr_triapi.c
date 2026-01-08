/*
pvr_triapi.c - TriAPI draw methods
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
#include "const.h"

static struct
{
	int		renderMode;		// override kRenderMode from TriAPI
	vec4_t		triRGBA;
} ds;

typedef struct tri_vert_s
{
	vec3_t pos;
	float u, v;
	uint32_t argb;
} tri_vert_t;

static tri_vert_t g_tri_verts[4096];
static int g_tri_count = 0;
static int g_tri_mode = TRI_TRIANGLES;
static float g_tri_u = 0.0f, g_tri_v = 0.0f;
static uint32_t g_tri_color = 0xFFFFFFFF;

static inline int TriAPI_GetList( void )
{
	if( g_pvr_current_list == PVR_LIST_OP_POLY || g_pvr_current_list == PVR_LIST_TR_POLY )
		return g_pvr_current_list;
	// fallback (shouldn't happen, but avoid wrong list if called unexpectedly)
	return ( ds.renderMode == kRenderNormal ) ? PVR_LIST_OP_POLY : PVR_LIST_TR_POLY;
}

static void TriAPI_SubmitTri( pvr_dr_state_t *dr_state, const tri_vert_t *a, const tri_vert_t *b, const tri_vert_t *c )
{
	// Load transform once per flush outside
	shz_vec4_t pa = shz_xmtrx_transform_vec4( shz_vec3_vec4( shz_vec3_init( a->pos[0], a->pos[1], a->pos[2] ), 1.0f ));
	shz_vec4_t pb = shz_xmtrx_transform_vec4( shz_vec3_vec4( shz_vec3_init( b->pos[0], b->pos[1], b->pos[2] ), 1.0f ));
	shz_vec4_t pc = shz_xmtrx_transform_vec4( shz_vec3_vec4( shz_vec3_init( c->pos[0], c->pos[1], c->pos[2] ), 1.0f ));

	PVR_ClipAndSubmitTriangle( dr_state,
		pa, pb, pc,
		a->u, a->v, b->u, b->v, c->u, c->v,
		a->argb, b->argb, c->argb );
}

static void TriAPI_Flush( void )
{
	__attribute__((aligned(8))) float aligned_matrix[16];

	if( g_tri_count < 3 )
	{
		g_tri_count = 0;
		return;
	}

	const int list = TriAPI_GetList();

	// pick bound texture if any (TriSpriteTexture/GL_Bind updates glState.currentTexturesIndex)
	int texnum = glState.currentTexturesIndex;
	gl_texture_t *glt = ( texnum > 0 ) ? R_GetTexture( texnum ) : NULL;

	pvr_dr_state_t dr_state;
	pvr_dr_init( &dr_state );

	// Build polygon context from TriRenderMode (mimic GL defaults)
	pvr_poly_cxt_t cxt;
	if( glt && glt->loaded && glt->vram_ptr )
	{
		pvr_poly_cxt_txr( &cxt, list, glt->format, glt->width, glt->height, glt->vram_ptr, PVR_FILTER_BILINEAR );
		cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
		cxt.txr.alpha = PVR_TXRALPHA_ENABLE;
		cxt.txr.uv_flip = PVR_UVFLIP_NONE;
		cxt.txr.uv_clamp = PVR_UVCLAMP_NONE;
		cxt.txr.mipmap = PVR_MIPMAP_DISABLE;
	}
	else
	{
		pvr_poly_cxt_col( &cxt, list );
	}

	cxt.gen.culling = PVR_CULLING_NONE;
	cxt.gen.fog_type = glState.isFogEnabled ? PVR_FOG_TABLE : PVR_FOG_DISABLE;
	cxt.gen.shading = PVR_SHADE_GOURAUD;
	cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;

	// renderMode -> alpha/blend/depthwrite
	if( ds.renderMode == kRenderNormal )
	{
		cxt.gen.alpha = PVR_ALPHA_DISABLE;
		cxt.depth.write = PVR_DEPTHWRITE_ENABLE;
	}
	else
	{
		cxt.gen.alpha = PVR_ALPHA_ENABLE;
		cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		if( ds.renderMode == kRenderTransAdd || ds.renderMode == kRenderGlow )
		{
			cxt.blend.src = PVR_BLEND_SRCALPHA;
			cxt.blend.dst = PVR_BLEND_ONE;
		}
		else
		{
			cxt.blend.src = PVR_BLEND_SRCALPHA;
			cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
		}
	}

	pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target( dr_state );
	pvr_poly_compile( hdr, &cxt );
	pvr_dr_commit( hdr );

	// load current matrix (expected viewproj in r_world_matrix during scene rendering)
	memcpy( aligned_matrix, r_world_matrix, sizeof( aligned_matrix ));
	shz_xmtrx_load_4x4((shz_mat4x4_t*)aligned_matrix);

	// Convert primitive stream to triangles and submit
	if( g_tri_mode == TRI_TRIANGLES )
	{
		for( int i = 0; i + 2 < g_tri_count; i += 3 )
			TriAPI_SubmitTri( &dr_state, &g_tri_verts[i], &g_tri_verts[i+1], &g_tri_verts[i+2] );
	}
	else if( g_tri_mode == TRI_TRIANGLE_STRIP )
	{
		for( int i = 2; i < g_tri_count; i++ )
			TriAPI_SubmitTri( &dr_state, &g_tri_verts[i-2], &g_tri_verts[i-1], &g_tri_verts[i] );
	}
	else if( g_tri_mode == TRI_TRIANGLE_FAN || g_tri_mode == TRI_POLYGON )
	{
		for( int i = 2; i < g_tri_count; i++ )
			TriAPI_SubmitTri( &dr_state, &g_tri_verts[0], &g_tri_verts[i-1], &g_tri_verts[i] );
	}
	else if( g_tri_mode == TRI_QUADS )
	{
		for( int i = 0; i + 3 < g_tri_count; i += 4 )
		{
			TriAPI_SubmitTri( &dr_state, &g_tri_verts[i], &g_tri_verts[i+1], &g_tri_verts[i+2] );
			TriAPI_SubmitTri( &dr_state, &g_tri_verts[i], &g_tri_verts[i+2], &g_tri_verts[i+3] );
		}
	}
	else if( g_tri_mode == TRI_LINES )
	{
		// Convert lines to billboard quads (thin rectangles facing camera)
		// Each line segment becomes a quad with width based on view distance
		for( int i = 0; i + 1 < g_tri_count; i += 2 )
		{
			const tri_vert_t *v0 = &g_tri_verts[i];
			const tri_vert_t *v1 = &g_tri_verts[i+1];
			
			// Compute line direction and perpendicular in world space
			vec3_t dir, right, up, perp;
			VectorSubtract( v1->pos, v0->pos, dir );
			float len = VectorLength( dir );
			if( len < 0.001f ) continue; // degenerate line
			
			VectorScale( dir, 1.0f / len, dir );
			
			// Use view right/up to build perpendicular
			VectorCopy( RI.cull_vright, right );
			VectorCopy( RI.cull_vup, up );
			
			// Build perpendicular vector (cross product of dir and view forward)
			CrossProduct( dir, RI.cull_vforward, perp );
			VectorNormalize( perp );
			
			// Line width (small fixed size, could be made configurable)
			float width = 0.5f;
			
			// Build quad corners
			vec3_t corners[4];
			VectorMA( v0->pos, -width, perp, corners[0] );
			VectorMA( v0->pos, width, perp, corners[1] );
			VectorMA( v1->pos, width, perp, corners[2] );
			VectorMA( v1->pos, -width, perp, corners[3] );
			
			// Submit as two triangles
			tri_vert_t quad[4];
			for( int j = 0; j < 4; j++ )
			{
				VectorCopy( corners[j], quad[j].pos );
				quad[j].u = (j < 2) ? 0.0f : 1.0f;
				quad[j].v = (j == 0 || j == 3) ? 0.0f : 1.0f;
				quad[j].argb = v0->argb; // use first vertex color
			}
			
			TriAPI_SubmitTri( &dr_state, &quad[0], &quad[1], &quad[2] );
			TriAPI_SubmitTri( &dr_state, &quad[0], &quad[2], &quad[3] );
		}
	}
	else if( g_tri_mode == TRI_POINTS )
	{
		// Convert points to billboard quads (small squares facing camera)
		for( int i = 0; i < g_tri_count; i++ )
		{
			const tri_vert_t *v = &g_tri_verts[i];
			
			// Point size (small fixed size, could be made configurable)
			float size = 1.0f;
			
			// Build quad corners using view right/up
			vec3_t corners[4];
			VectorMA( v->pos, -size, RI.cull_vright, corners[0] );
			VectorMA( corners[0], -size, RI.cull_vup, corners[0] );
			
			VectorMA( v->pos, size, RI.cull_vright, corners[1] );
			VectorMA( corners[1], -size, RI.cull_vup, corners[1] );
			
			VectorMA( v->pos, size, RI.cull_vright, corners[2] );
			VectorMA( corners[2], size, RI.cull_vup, corners[2] );
			
			VectorMA( v->pos, -size, RI.cull_vright, corners[3] );
			VectorMA( corners[3], size, RI.cull_vup, corners[3] );
			
			// Submit as two triangles
			tri_vert_t quad[4];
			for( int j = 0; j < 4; j++ )
			{
				VectorCopy( corners[j], quad[j].pos );
				quad[j].u = (j < 2) ? 0.0f : 1.0f;
				quad[j].v = (j == 0 || j == 3) ? 0.0f : 1.0f;
				quad[j].argb = v->argb;
			}
			
			TriAPI_SubmitTri( &dr_state, &quad[0], &quad[1], &quad[2] );
			TriAPI_SubmitTri( &dr_state, &quad[0], &quad[2], &quad[3] );
		}
	}
	else if( g_tri_mode == TRI_QUAD_STRIP )
	{
		// Convert quad strip to triangles
		for( int i = 0; i + 3 < g_tri_count; i += 2 )
		{
			TriAPI_SubmitTri( &dr_state, &g_tri_verts[i], &g_tri_verts[i+1], &g_tri_verts[i+2] );
			TriAPI_SubmitTri( &dr_state, &g_tri_verts[i+1], &g_tri_verts[i+3], &g_tri_verts[i+2] );
		}
	}

	pvr_dr_finish();
	g_tri_count = 0;
}

/*
===============================================================

  TRIAPI IMPLEMENTATION

===============================================================
*/
/*
=============
TriRenderMode

set rendermode
=============
*/
void R_TriRenderMode( int mode )
{
	ds.renderMode = mode;
}

/*
=============
TriBegin

begin triangle sequence
=============
*/
void TriBegin( int mode )
{
	g_tri_mode = mode;
	g_tri_count = 0;
}

/*
=============
TriEnd

draw triangle sequence
=============
*/
void TriEnd( void )
{
	TriAPI_Flush();
}

/*
=============
_TriColor4f

=============
*/
void _TriColor4f( float r, float g, float b, float a )
{
	// Convert float color to ARGB (like GL would after modulation rules handled by engine wrapper)
	byte R = (byte)bound( 0, (int)(r * 255.0f), 255 );
	byte G = (byte)bound( 0, (int)(g * 255.0f), 255 );
	byte B = (byte)bound( 0, (int)(b * 255.0f), 255 );
	byte A = (byte)bound( 0, (int)(a * 255.0f), 255 );
	glState.currentColor = (A << 24) | (R << 16) | (G << 8) | B;
	g_tri_color = glState.currentColor;
}

/*
=============
_TriColor4f

=============
*/
void _TriColor4ub( byte r, byte g, byte b, byte a )
{
	// Store color in ARGB format: 0xAARRGGBB
	glState.currentColor = (a << 24) | (r << 16) | (g << 8) | b;
	g_tri_color = glState.currentColor;
}


/*
=============
TriColor4ub

=============
*/
void TriColor4ub( byte r, byte g, byte b, byte a )
{
	ds.triRGBA[0] = r * (1.0f / 255.0f);
	ds.triRGBA[1] = g * (1.0f / 255.0f);
	ds.triRGBA[2] = b * (1.0f / 255.0f);
	ds.triRGBA[3] = a * (1.0f / 255.0f);
	_TriColor4ub( r, g, b, a );
}

/*
=================
TriColor4f
=================
*/
void TriColor4f( float r, float g, float b, float a )
{
	if( ds.renderMode == kRenderTransAlpha )
		TriColor4ub( r * 255.9f, g * 255.9f, b * 255.9f, a * 255.0f );
	else _TriColor4f( r * a, g * a, b * a, 1.0 );

	ds.triRGBA[0] = r;
	ds.triRGBA[1] = g;
	ds.triRGBA[2] = b;
	ds.triRGBA[3] = a;
}

/*
=============
TriTexCoord2f

=============
*/
void TriTexCoord2f( float u, float v )
{
	g_tri_u = u;
	g_tri_v = v;
}

/*
=============
TriVertex3fv

=============
*/
void TriVertex3fv( const float *v )
{
	if( g_tri_count >= (int)(sizeof( g_tri_verts ) / sizeof( g_tri_verts[0] )))
		TriAPI_Flush();

	VectorCopy( v, g_tri_verts[g_tri_count].pos );
	g_tri_verts[g_tri_count].u = g_tri_u;
	g_tri_verts[g_tri_count].v = g_tri_v;
	g_tri_verts[g_tri_count].argb = g_tri_color;
	g_tri_count++;
}

/*
=============
TriVertex3f

=============
*/
void TriVertex3f( float x, float y, float z )
{
	if( g_tri_count >= (int)(sizeof( g_tri_verts ) / sizeof( g_tri_verts[0] )))
		TriAPI_Flush();

	g_tri_verts[g_tri_count].pos[0] = x;
	g_tri_verts[g_tri_count].pos[1] = y;
	g_tri_verts[g_tri_count].pos[2] = z;
	g_tri_verts[g_tri_count].u = g_tri_u;
	g_tri_verts[g_tri_count].v = g_tri_v;
	g_tri_verts[g_tri_count].argb = g_tri_color;
	g_tri_count++;
}

/*
=============
TriWorldToScreen

convert world coordinates (x,y,z) into screen (x, y)
=============
*/
int TriWorldToScreen( const float *world, float *screen )
{
	int	retval;

	retval = R_WorldToScreen( world, screen );

	screen[0] =  0.5f * screen[0] * (float)RI.viewport[2];
	screen[1] = -0.5f * screen[1] * (float)RI.viewport[3];
	screen[0] += 0.5f * (float)RI.viewport[2];
	screen[1] += 0.5f * (float)RI.viewport[3];

	return retval;
}

/*
=============
TriSpriteTexture

bind current texture
=============
*/
int TriSpriteTexture( model_t *pSpriteModel, int frame )
{
	int	gl_texturenum;

	if(( gl_texturenum = R_GetSpriteTexture( pSpriteModel, frame )) == 0 )
		return 0;

	if( gl_texturenum <= 0 || gl_texturenum >= MAX_TEXTURES )
		gl_texturenum = tr.defaultTexture;

	GL_Bind( XASH_TEXTURE0, gl_texturenum );

	return 1;
}

/*
=============
TriFog

enables global fog on the level
=============
*/
void TriFog( float flFogColor[3], float flStart, float flEnd, int bOn )
{
	// overrided by internal fog
	if( RI.fogEnabled || !gl_fog.value ) return;
	RI.fogCustom = bOn;

	// check for invalid parms
	if( flEnd <= flStart )
	{
		glState.isFogEnabled = RI.fogCustom = false;
		return;
	}

	// copy fog params
	RI.fogColor[0] = flFogColor[0] / 255.0f;
	RI.fogColor[1] = flFogColor[1] / 255.0f;
	RI.fogColor[2] = flFogColor[2] / 255.0f;
	RI.fogColor[3] = 1.0f;

	RI.fogStart = flStart;
	RI.fogEnd = flEnd;

	if( RI.fogDensity > 0.0f )
	{
		// Exponential fog (EXP2 mode)
		// Fog is already handled by PVR fog table, so we just set the params
	}
	else
	{
		// Linear fog
		RI.fogSkybox = true;
	}

	// Fog state is updated in R_CheckFog() / R_AllowFog() which sets glState.isFogEnabled
	// PVR fog table will use RI.fogStart/RI.fogEnd/RI.fogColor when enabled
}

/*
=============
TriGetMatrix

very strange export
=============
*/
void TriGetMatrix( const int pname, float *matrix )
{
	// Convert GL matrix enum to our matrix storage
	// Note: GL uses row-major, we use column-major (sh4zam convention)
	// This is a legacy API, so we provide basic support
	
	if( !matrix ) return;
	
	// Map GL matrix types to our matrices (column-major)
	// For compatibility, we transpose to row-major format (GL convention)
	// GL_MODELVIEW_MATRIX = 0x0BA6, GL_PROJECTION_MATRIX = 0x0BA7
	// shz_mat4x4_t stores matrix in column-major format: elem[0-3]=col0, elem[4-7]=col1, elem[8-11]=col2, elem[12-15]=col3
	switch( pname )
	{
	case 0x0BA6: // GL_MODELVIEW_MATRIX
		{
			// Return modelview matrix (worldview * object)
			shz_mat4x4_t *m = &RI.modelviewMatrix;
			// Convert column-major to row-major: row i, col j -> elem[j*4 + i]
			for( int i = 0; i < 4; i++ )
				for( int j = 0; j < 4; j++ )
					matrix[i * 4 + j] = m->elem[j * 4 + i];
		}
		break;
	case 0x0BA7: // GL_PROJECTION_MATRIX
		{
			// Return projection matrix
			shz_mat4x4_t *m = &RI.projectionMatrix;
			// Convert column-major to row-major: row i, col j -> elem[j*4 + i]
			for( int i = 0; i < 4; i++ )
				for( int j = 0; j < 4; j++ )
					matrix[i * 4 + j] = m->elem[j * 4 + i];
		}
		break;
	case 0x1702: // GL_TEXTURE_MATRIX (not used in this renderer, return identity)
	default:
		// Return identity matrix (row-major)
		memset( matrix, 0, 16 * sizeof( float ));
		matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
		break;
	}
}

/*
=============
TriForParams

=============
*/
void TriFogParams( float flDensity, int iFogSkybox )
{
	RI.fogDensity = flDensity;
	RI.fogSkybox = iFogSkybox;
}

/*
=============
TriCullFace

=============
*/
void TriCullFace( TRICULLSTYLE mode )
{
#if 0
	int glMode;

	switch( mode )
	{
	case TRI_FRONT:
		glMode = GL_FRONT;
		break;
	case TRI_BACK:
		glMode = GL_BACK;
		break;
	case TRI_NONE:
	default:
		glMode = GL_NONE;
		break;
	}

	GL_Cull( glMode );
#endif
}

/*
=============
TriBrightness
=============
*/
void TriBrightness( float brightness )
{
	float	r, g, b;

	r = ds.triRGBA[0] * ds.triRGBA[3] * brightness;
	g = ds.triRGBA[1] * ds.triRGBA[3] * brightness;
	b = ds.triRGBA[2] * ds.triRGBA[3] * brightness;

	_TriColor4f( r, g, b, 1.0f );
}

