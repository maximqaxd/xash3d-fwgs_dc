/*
vid_dc.c - DC vid component
Copyright (C) 2024 maximqad

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "platform/platform.h"
#if XASH_VIDEO == VIDEO_KOS
#include "input.h"
#include "client.h"
#include "filesystem.h"
#include "vid_common.h"
#include <kos.h>

static int num_vidmodes = 0;
static void GL_SetupAttributes( void );
static qboolean vsync;

pvr_init_params_t params = {
	// Real HW: 8-word bins are often too small and can result in missing geometry / blank output.
	// Use 16-word bins for enabled lists (OP/TR/PT).
	{ PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16 },
	2536 * 256,    /* vertex buffer */
	0,             /* dma disabled for TA  */
	0,             /* fsaa off */
	0,             /* keep PVR translucent autosort OFF  */
	2            /* OPB count: start with 2*/
};

/*
==================
GL_SetupAttributes
==================
*/
static void GL_SetupAttributes( void )
{
	ref.dllFuncs.GL_SetupAttributes( glw_state.safe );
}

int GL_SetAttribute(int attr, int val)
{
    switch (attr) 
    {
        case REF_GL_RED_SIZE:
            val = 5; 
            break;
        case REF_GL_GREEN_SIZE:
            val = 6; 
            break;
        case REF_GL_BLUE_SIZE:
            val = 5; 
            break;
        case REF_GL_ALPHA_SIZE:
            val = 0; 
            break;
        case REF_GL_DOUBLEBUFFER:
            val = 1; 
            break;
        case REF_GL_DEPTH_SIZE:
        	val = 16; 
            break;
        case REF_GL_STENCIL_SIZE:
            val = 0; 
            break;
        case REF_GL_MULTISAMPLEBUFFERS:
            break;
        case REF_GL_MULTISAMPLESAMPLES:
            break;
        case REF_GL_ACCELERATED_VISUAL:
            break;
        default:
            return -1; // Unsupported 
    }

    return 0; 
}

int GL_GetAttribute(int attr, int *val)
{
	int value;

    switch (attr) 
    {
        case REF_GL_RED_SIZE:
            value = 5; 
            break;
        case REF_GL_GREEN_SIZE:
            value = 6; 
            break;
        case REF_GL_BLUE_SIZE:
            value = 5; 
            break;
        case REF_GL_ALPHA_SIZE:
            value = 0; 
            break;
        case REF_GL_DOUBLEBUFFER:
            value = 1; 
            break;
        case REF_GL_DEPTH_SIZE:
        	value = 16; 
            break;
        case REF_GL_STENCIL_SIZE:
            value = 0; 
            break;
        case REF_GL_MULTISAMPLEBUFFERS:
            break;
        case REF_GL_MULTISAMPLESAMPLES:
            break;
        case REF_GL_ACCELERATED_VISUAL:
            break;
        default:
            return -1; // Unsupported 
    }

	*val = value;
    
    return 0; 
}

void GL_SwapBuffers( void )
{

}

void DC_GetScreenRes( int *x, int *y )
{
	*x = 640; 
    *y = 480; 
}
/*
=================
GL_DeleteContext

always return false
=================
*/
qboolean GL_DeleteContext( void )
{
	return false;
}

/*
=================
GL_CreateContext
=================
*/
static qboolean GL_CreateContext( void )
{
	return true;
}

/*
=================
GL_UpdateContext
=================
*/
static qboolean GL_UpdateContext( void )
{
	return true;
}

qboolean R_Init_Video( const int type )
{
	qboolean retval;
	int pvr_rc;

	if( type != REF_GL ) // software not supported 
		return false;

	refState.desktopBitsPixel = 16;

	if( !(retval = VID_SetMode()) )
	{
		return retval;
	}

	pvr_rc = pvr_init( &params );
	if( pvr_rc < 0 )
	{
		// If PVR init fails, all scene/list submission will be ignored -> black screen on real hardware.
		Sys_Error( "%s: pvr_init failed (rc=%d)\n", __func__, pvr_rc );
		return false;
	}
	host.renderinfo_changed = false;
	glw_state.safe = 0;
	GL_SetupAttributes( );
	ref.dllFuncs.GL_InitExtensions();
	return true;
}

void R_Free_Video( void )
{
	// Free any allocated video resources if necessary
	// VID_DestroyWindow();
	// R_FreeVideoModes();
	ref.dllFuncs.GL_ClearExtensions();
}

qboolean VID_SetMode(void) {
    int8_t cable_type = vid_check_cable(); // Get the current cable type
    int dm; // Display mode
    vid_pixel_mode_t pm = PM_RGB565;  // pixel mode

    switch (cable_type) {
        case CT_VGA:
            dm = DM_640x480_VGA; // VGA mode
            break;
        case CT_RGB:
            dm = DM_640x480; // RGB mode
            break;
        case CT_COMPOSITE:
            dm = DM_640x480; // Composite mode
            break;
        case CT_NONE:
        default:
            Sys_Error("%s: No valid video cable connected.\n Check your video cable connection.\n", __func__);
            return false; 
    }


    vid_init(dm, pm); 

    return true; 
}

rserr_t R_ChangeDisplaySettings( int width, int height, window_mode_t window_mode )
{
	int render_w, render_h;

	DC_GetScreenRes( &width, &height );

	render_w = width;
	render_h = height;

	Con_Reportf( "%s: forced resolution to %dx%d)\n", __func__, width, height );

	VID_SetDisplayTransform( &render_w, &render_h );
	R_SaveVideoMode( width, height, render_w, render_h, true );

	return rserr_ok;
}


int R_MaxVideoModes( void )
{
    // stub
	return 1; 
}

vidmode_t* R_GetVideoMode( int num )
{
    //stub
	return NULL;
}

void* GL_GetProcAddress( const char *name ) // RenderAPI requirement
{
	//stub
	return NULL;
}

void GL_UpdateSwapInterval( void )
{
	// disable VSync while level is loading
	if( cls.state < ca_active )
	{
		// setup vsync here
		vsync = false;
		SetBits( gl_vsync.flags, FCVAR_CHANGED );
	}
	else if( FBitSet( gl_vsync.flags, FCVAR_CHANGED ))
	{
		ClearBits( gl_vsync.flags, FCVAR_CHANGED );
		vsync = true;
	}
}

void *SW_LockBuffer( void )
{
	// stub
	return NULL;
}

void SW_UnlockBuffer( void )
{
	// stub
}

qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	// stub
	return false;
}
#endif // XASH_VIDEO == VIDEO_KOS
