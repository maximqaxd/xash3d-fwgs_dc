/*
pvr_draw.c - orthogonal drawing stuff
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
#include <dc/pvr.h>
#include <sh4zam/shz_sh4zam.h>


/*
=============
R_GetImageParms
=============
*/
void R_GetTextureParms( int *w, int *h, int texnum )
{
	gl_texture_t	*glt;

	glt = R_GetTexture( texnum );
	if( w ) *w = glt->srcWidth;
	if( h ) *h = glt->srcHeight;
}

/*
=============
R_GetSpriteParms

same as GetImageParms but used
for sprite models
=============
*/
void R_GetSpriteParms( int *frameWidth, int *frameHeight, int *numFrames, int currentFrame, const model_t *pSprite )
{
	mspriteframe_t	*pFrame;

	if( !pSprite || pSprite->type != mod_sprite ) return; // bad model ?
	pFrame = R_GetSpriteFrame( pSprite, currentFrame, 0.0f );

	if( frameWidth ) *frameWidth = pFrame->width;
	if( frameHeight ) *frameHeight = pFrame->height;
	if( numFrames ) *numFrames = pSprite->numframes;
}

int R_GetSpriteTexture( const model_t *m_pSpriteModel, int frame )
{
	if( !m_pSpriteModel || m_pSpriteModel->type != mod_sprite || !m_pSpriteModel->cache.data )
		return 0;

	return R_GetSpriteFrame( m_pSpriteModel, frame, 0.0f )->gl_texturenum;
}

// 2D Batching System
#define MAX_2D_VERTICES 512

typedef struct {
    float x, y, z;
    float u, v;
    uint32_t color;
} vertex_2d_t;

typedef struct {
    vertex_2d_t vertices[MAX_2D_VERTICES];
    int vertex_count;
    int current_texture;
    pvr_ptr_t current_tex_addr;
    uint32_t current_tex_format;
    int current_tex_width;
    int current_tex_height;
    int current_tex_flags;
    int current_filter;
    int list_open;
    int render_mode;  // kRenderNormal, kRenderTransColor, etc. (from GL_SetRenderMode)
} batch_2d_t;

static batch_2d_t batch_2d;

/*
===============
Batch_Flush
Internal function to flush current batch
===============
*/
static void Batch_Flush(void) 
{
    if (batch_2d.vertex_count == 0)
        return;
    
    // Make sure we have a list open
    if (!batch_2d.list_open)
    {
        pvr_list_begin(PVR_LIST_PT_POLY);
        batch_2d.list_open = 1;
    }
    
    pvr_dr_state_t dr_state;
    pvr_dr_init(&dr_state);
    
    // Setup polygon context based on whether we have a texture
    pvr_poly_cxt_t cxt;
    
    // Check texture format and 2D render mode (GL_SetRenderMode sets glState.renderMode2D)
    uint32_t base_format = (batch_2d.current_texture > 0 && batch_2d.current_tex_addr) 
                          ? (batch_2d.current_tex_format & 0x38000000) : 0;
    qboolean is_onebit_alpha = (base_format == PVR_TXRFMT_ARGB1555);
    qboolean is_smooth_alpha = (base_format == PVR_TXRFMT_ARGB4444);
    qboolean is_rgb565 = (base_format == PVR_TXRFMT_RGB565);
    int mode = batch_2d.render_mode;
    
    if (batch_2d.current_texture > 0 && batch_2d.current_tex_addr)
    {
        // Textured polygon
        pvr_poly_cxt_txr(&cxt, PVR_LIST_PT_POLY, 
                        batch_2d.current_tex_format,
                        batch_2d.current_tex_width, 
                        batch_2d.current_tex_height,
                        batch_2d.current_tex_addr, 
                        batch_2d.current_filter ? PVR_FILTER_BILINEAR : PVR_FILTER_NONE);


        /*
         * GL render mode -> PVR mapping (from ref/gl/gl_backend.c GL_SetRenderMode):
         * GL                              PVR blend              PVR texenv      Alpha test
         * kRenderNormal                   no blend (replace)     MODULATE        no
         * kRenderTransColor               SRCALPHA, INVSRCALPHA  MODULATE        yes (1-bit)
         * kRenderTransTexture             SRCALPHA, INVSRCALPHA  MODULATE        no
         * kRenderTransAlpha               no blend               REPLACE/MOD     yes
         * kRenderGlow / kRenderTransAdd   SRCALPHA, ONE          MODULATEALPHA   no
         * kRenderScreenFadeModulate       ZERO, SRC_COLOR         MODULATE        no
         */
        switch (mode)
        {
        case kRenderNormal:
            cxt.txr.env = PVR_TXRENV_MODULATE;
            if (is_onebit_alpha || is_smooth_alpha)
                PVR_SET(0x11C, 1);
            break;
        case kRenderTransColor:
            /* DrawHoles / crosshair: vertex color tints; no alpha test (rely on blend for holes) */
            cxt.txr.env = (is_onebit_alpha || is_rgb565) ? PVR_TXRENV_MODULATE : PVR_TXRENV_MODULATEALPHA;
            break;
        case kRenderTransTexture:
            /* Fonts, console: src*a + dest*(1-a) */
            cxt.txr.env = is_smooth_alpha ? PVR_TXRENV_MODULATEALPHA : PVR_TXRENV_MODULATE;
            break;
        case kRenderTransAlpha:
            /* 1-bit alpha sprites (no blend) */
            if (is_onebit_alpha || is_smooth_alpha)
                PVR_SET(0x11C, 1);
            cxt.txr.env = PVR_TXRENV_REPLACE;
            break;
        case kRenderGlow:
        case kRenderTransAdd:
            /* Weapon icons, HUD numbers: src*a + dest (additive) */
            cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
            break;
        default:
            if (mode == (int)kRenderScreenFadeModulate) {
                /* Screen fade: dest * src_color */
                cxt.txr.env = PVR_TXRENV_MODULATE;
                break;
            }
            /* Fallback: normal alpha blend */
            if (is_onebit_alpha || is_smooth_alpha)
                PVR_SET(0x11C, 1);
            cxt.txr.env = is_rgb565 ? PVR_TXRENV_MODULATE : PVR_TXRENV_MODULATEALPHA;
            break;
        }
    } else {
        pvr_poly_cxt_col(&cxt, PVR_LIST_PT_POLY);
    }
    
    cxt.gen.culling = PVR_CULLING_NONE;
    cxt.gen.fog_type = PVR_FOG_DISABLE;
    /* 2D HUD must always pass depth and not write it (match CL_FillRGBA); else crosshair/weapon quads can be behind 3D */
    cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    
    /* Blend: match GL_SetRenderMode exactly */
    switch (mode)
    {
    case kRenderNormal:
        /* GL: no blend -> replace (src only) */
        cxt.blend.src = PVR_BLEND_ONE;
        cxt.blend.dst = PVR_BLEND_ZERO;
        break;
    case kRenderTransColor:
    case kRenderTransTexture:
        /* GL: SRC_ALPHA, ONE_MINUS_SRC_ALPHA */
        cxt.blend.src = PVR_BLEND_SRCALPHA;
        cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
        break;
    case kRenderTransAlpha:
        /* GL: no blend */
        cxt.blend.src = PVR_BLEND_ONE;
        cxt.blend.dst = PVR_BLEND_ZERO;
        break;
    case kRenderGlow:
    case kRenderTransAdd:
        /* Weapon icons / HUD additive: true additive (ONE, ONE) so icons visible */
        cxt.blend.src = PVR_BLEND_ONE;
        cxt.blend.dst = PVR_BLEND_ONE;
        break;
    default:
        if (mode == (int)kRenderScreenFadeModulate) {
            /* GL: ZERO, SRC_COLOR (dest * src); PVR has no SRCCOLOR, use alpha blend fallback */
            cxt.blend.src = PVR_BLEND_SRCALPHA;
            cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
            break;
        }
        cxt.blend.src = PVR_BLEND_SRCALPHA;
        cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
        break;
    }
    
    cxt.gen.alpha = PVR_ALPHA_ENABLE;
    
    // Submit header
    pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
    pvr_poly_compile(hdr, &cxt);
    pvr_dr_commit(hdr);
    
    // Submit all vertices as triangle strips (quads); screen pixel coords as-is
    for (int i = 0; i < batch_2d.vertex_count; i += 4) {
        pvr_vertex_t *vert;

        vert = (pvr_vertex_t *)pvr_dr_target(dr_state);
        vert->flags = PVR_CMD_VERTEX;
        vert->x = batch_2d.vertices[i].x;
        vert->y = batch_2d.vertices[i].y;
        vert->z = batch_2d.vertices[i].z;
        vert->u = batch_2d.vertices[i].u;
        vert->v = batch_2d.vertices[i].v;
        vert->argb = batch_2d.vertices[i].color;
        vert->oargb = 0;
        pvr_dr_commit(vert);

        vert = (pvr_vertex_t *)pvr_dr_target(dr_state);
        vert->flags = PVR_CMD_VERTEX;
        vert->x = batch_2d.vertices[i+1].x;
        vert->y = batch_2d.vertices[i+1].y;
        vert->z = batch_2d.vertices[i+1].z;
        vert->u = batch_2d.vertices[i+1].u;
        vert->v = batch_2d.vertices[i+1].v;
        vert->argb = batch_2d.vertices[i+1].color;
        vert->oargb = 0;
        pvr_dr_commit(vert);

        vert = (pvr_vertex_t *)pvr_dr_target(dr_state);
        vert->flags = PVR_CMD_VERTEX;
        vert->x = batch_2d.vertices[i+2].x;
        vert->y = batch_2d.vertices[i+2].y;
        vert->z = batch_2d.vertices[i+2].z;
        vert->u = batch_2d.vertices[i+2].u;
        vert->v = batch_2d.vertices[i+2].v;
        vert->argb = batch_2d.vertices[i+2].color;
        vert->oargb = 0;
        pvr_dr_commit(vert);

        vert = (pvr_vertex_t *)pvr_dr_target(dr_state);
        vert->flags = PVR_CMD_VERTEX_EOL;
        vert->x = batch_2d.vertices[i+3].x;
        vert->y = batch_2d.vertices[i+3].y;
        vert->z = batch_2d.vertices[i+3].z;
        vert->u = batch_2d.vertices[i+3].u;
        vert->v = batch_2d.vertices[i+3].v;
        vert->argb = batch_2d.vertices[i+3].color;
        vert->oargb = 0;
        pvr_dr_commit(vert);
    }
    
    pvr_dr_finish();
    
    // Reset batch
    batch_2d.vertex_count = 0;
}

/*
===============
Batch_SetTexture
Internal function to set current texture
===============
*/
static void Batch_SetTexture(int texnum) {
    // If texture changed, flush current batch
    if (texnum != batch_2d.current_texture) {
        Batch_Flush();
        
        batch_2d.current_texture = texnum;
        
        if (texnum > 0 && texnum < MAX_TEXTURES) {
            gl_texture_t *glt = R_GetTexture(texnum);
            if (glt && glt->loaded && glt->vram_ptr) {
                batch_2d.current_tex_addr = glt->vram_ptr;
                batch_2d.current_tex_format = glt->format;
                batch_2d.current_tex_width = glt->width;
                batch_2d.current_tex_height = glt->height;
                batch_2d.current_tex_flags = glt->flags;
                // Match Xash behavior: fonts/nearest flagged textures stay crisp
                batch_2d.current_filter = GL_TextureFilteringEnabled(glt) ? 1 : 0;
            } else {
                batch_2d.current_tex_addr = NULL;
            }
        } else {
            batch_2d.current_tex_addr = NULL;
        }
    }
}

/*
===============
Draw_FlushBatch
Flush pending 2D draws. Do not close the PVR list (each list only once per frame on KOS).
===============
*/
void Draw_FlushBatch(void) {
    Batch_Flush();
}

/*
===============
Draw_AddQuad
Add a textured quad to the batch
===============
*/
static void Draw_AddQuad(float x1, float y1, float x2, float y2,
                  float s1, float t1, float s2, float t2, int texnum) {
    
    // Skip if no texture
    if (texnum <= 0 || texnum >= MAX_TEXTURES)
        return;
    
    gl_texture_t *glt = R_GetTexture(texnum);
    // Skip if texture not loaded
    if (!glt || !glt->loaded || !glt->vram_ptr)
        return;
    
    // Flush if render mode changed (e.g. crosshair kRenderTransColor, then text kRenderNormal)
    if (batch_2d.vertex_count > 0 && batch_2d.render_mode != glState.renderMode2D)
        Batch_Flush();
    if (batch_2d.vertex_count == 0)
        batch_2d.render_mode = glState.renderMode2D;
    
    // Check if we need to flush (texture change or buffer full)
    if (batch_2d.vertex_count + 4 > MAX_2D_VERTICES)
        Batch_Flush();
    
    // Set the texture (will flush if different)
    Batch_SetTexture(texnum);
    
    // Add quad vertices (as triangle strip order: TL, TR, BL, BR)
    int idx = batch_2d.vertex_count;
    
    // Use current color from glState (default to white if not set)
    uint32_t color = glState.currentColor;
    if (color == 0) color = 0xFFFFFFFF;  // Default to white if not set
    
    // Console/text use kRenderNormal or kRenderTransTexture -> z=1.0 (on top); HUD (radar, crosshair) -> z=0.99
    int mode = batch_2d.render_mode;
    float z = (mode == kRenderNormal || mode == kRenderTransTexture) ? 1.0f : 0.99f;
    
    // Store screen pixel coords as-is; PT list uses current matrix (set elsewhere for 2D if needed)
    // Top-left
    batch_2d.vertices[idx].x = x1;
    batch_2d.vertices[idx].y = y1;
    batch_2d.vertices[idx].z = z;
    batch_2d.vertices[idx].u = s1;
    batch_2d.vertices[idx].v = t1;
    batch_2d.vertices[idx].color = color;
    
    // Top-right
    batch_2d.vertices[idx+1].x = x2;
    batch_2d.vertices[idx+1].y = y1;
    batch_2d.vertices[idx+1].z = z;
    batch_2d.vertices[idx+1].u = s2;
    batch_2d.vertices[idx+1].v = t1;
    batch_2d.vertices[idx+1].color = color;
    
    // Bottom-left
    batch_2d.vertices[idx+2].x = x1;
    batch_2d.vertices[idx+2].y = y2;
    batch_2d.vertices[idx+2].z = z;
    batch_2d.vertices[idx+2].u = s1;
    batch_2d.vertices[idx+2].v = t2;
    batch_2d.vertices[idx+2].color = color;
    
    // Bottom-right
    batch_2d.vertices[idx+3].x = x2;
    batch_2d.vertices[idx+3].y = y2;
    batch_2d.vertices[idx+3].z = z;
    batch_2d.vertices[idx+3].u = s2;
    batch_2d.vertices[idx+3].v = t2;
    batch_2d.vertices[idx+3].color = color;
    
    batch_2d.vertex_count += 4;
}

/*
=============
R_DrawStretchPic
=============
*/
void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum )
{
	Draw_AddQuad(x, y, x + w, y + h, s1, t1, s2, t2, texnum);
}

/*
=============
R_DrawStretchRaw
=============
*/
void R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty )
{

}

/*
=============
R_UploadStretchRaw
=============
*/
void R_UploadStretchRaw( int texture, int cols, int rows, int width, int height, const byte *data )
{

}

/*
===============
R_Set2DMode
===============
*/
void R_Set2DMode( qboolean enable )
{
	if( enable )
	{
		if( glState.in2DMode )
			return;

		// Initialize batch
		memset(&batch_2d, 0, sizeof(batch_2d));
		batch_2d.current_texture = -1;

		// Initialize color to white (default)
		glState.currentColor = 0xFFFFFFFF;

		// 2D batch stores screen pixel coords; matrix for PT list is set when list is used.

		glState.in2DMode = true;
		RI.currententity = NULL;
		RI.currentmodel = NULL;
	}
	else
	{
		if( !glState.in2DMode )
			return;

		// Flush any pending draws and close PT_POLY list (only place we close it per frame)
		Draw_FlushBatch();
		if (batch_2d.list_open) {
			pvr_list_finish();
			batch_2d.list_open = 0;
		}

		glState.in2DMode = false;
	}
}
