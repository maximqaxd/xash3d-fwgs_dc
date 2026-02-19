/*
cl_video.c - avi video player
Copyright (C) 2009 Uncle Mike

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
#include "client.h"
#if XASH_DREAMCAST
#include "avi.h"
#include "avi/mpeg.h"
#include <dc/pvr.h>


int mpeg_player_start_audio(mpeg_player_t *player);
void mpeg_player_poll_audio(mpeg_player_t *player);
void mpeg_player_stop_audio(mpeg_player_t *player);
mpeg_decode_result_t mpeg_decode_step(mpeg_player_t *player);
#endif

/*
=================================================================

AVI PLAYING

=================================================================
*/

static int		xres, yres;
static float		video_duration;
static float		cin_time;
static int		cin_frame;
static wavdata_t		cin_audio;
static movie_state_t	*cin_state;

/*
==================
SCR_NextMovie

Called when a demo or cinematic finishes
If the "nextmovie" cvar is set, that command will be issued
==================
*/
qboolean SCR_NextMovie( void )
{
	string	str;

	if( cls.movienum == -1 )
	{
		S_StopAllSounds( true );
		SCR_StopCinematic();
		CL_CheckStartupDemos();
		return false; // don't play movies
	}

	if( !cls.movies[cls.movienum][0] || cls.movienum == MAX_MOVIES )
	{
		S_StopAllSounds( true );
		SCR_StopCinematic();
		cls.movienum = -1;
		CL_CheckStartupDemos();
		return false;
	}


	Q_snprintf( str, MAX_STRING, "movie %s full\n", cls.movies[cls.movienum] );
	Cbuf_InsertText( str );
	cls.movienum++;

	return true;
}

static void SCR_CreateStartupVids( void )
{
	dc_file_t	*f;

	f = FS_Open( DEFAULT_VIDEOLIST_PATH, "w", false );
	if( !f ) return;

	// make standard video playlist: sierra, valve
#if XASH_DREAMCAST
	FS_Print( f, "media/sierra.mpg\n" );
	FS_Print( f, "media/valve.mpg\n" );
#else
	FS_Print( f, "media/sierra.avi\n" );
	FS_Print( f, "media/valve.avi\n" );
#endif
	FS_Close( f );
}

void SCR_CheckStartupVids( void )
{
	int	c = 0;
	byte *afile;
	char *pfile;
	string	token;
	
#if !XASH_DREAMCAST
	if( Sys_CheckParm( "-nointro" ) || host_developer.value || cls.demonum != -1 || GameState->nextstate != STATE_RUNFRAME )
	{
		// don't run movies where we in developer-mode
		cls.movienum = -1;
		CL_CheckStartupDemos();
		return;
	}
#endif

	if( !FS_FileExists( DEFAULT_VIDEOLIST_PATH, false ))
	{
		SCR_CreateStartupVids();
	}

	afile = FS_LoadFile( DEFAULT_VIDEOLIST_PATH, NULL, false );
	if( !afile )
	{
		Con_Printf( S_ERROR "SCR_CheckStartupVids: failed to load %s\n", DEFAULT_VIDEOLIST_PATH );
		return; // something bad happens
	}

	pfile = (char *)afile;

	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		Q_strncpy( cls.movies[c], token, sizeof( cls.movies[0] ));

		if( ++c > MAX_MOVIES - 1 )
		{
			Con_Printf( S_WARN "too many movies (%d) specified in %s\n", MAX_MOVIES, DEFAULT_VIDEOLIST_PATH );
			break;
		}
	}

	Mem_Free( afile );

	if( c == 0 )
	{
		Con_Printf( S_WARN "SCR_CheckStartupVids: no movies found in %s\n", DEFAULT_VIDEOLIST_PATH );
		return;
	}

	// run cinematic
	cls.movienum = 0;
	SCR_NextMovie ();
	Cbuf_Execute();
}

/*
==================
SCR_RunCinematic
==================
*/
void SCR_RunCinematic( void )
{
	if( cls.state != ca_cinematic )
		return;

	if( !AVI_IsActive( cin_state ))
	{
#if XASH_DREAMCAST
		// Ensure audio is stopped before moving to next movie
		if( cin_state )
		{
			mpeg_player_t *player = (mpeg_player_t *)AVI_GetMpegPlayer( cin_state );
			if( player )
				mpeg_player_stop_audio( player );
		}
#endif
		SCR_NextMovie( );
		return;
	}

	if( UI_IsVisible( ))
	{
		// these can happens when user set +menu_ option to cmdline
#if XASH_DREAMCAST
		// Stop MPEG audio immediately when video is skipped
		if( cin_state && AVI_IsActive( cin_state ))
		{
			mpeg_player_t *player = (mpeg_player_t *)AVI_GetMpegPlayer( cin_state );
			if( player )
			{
				// Stop audio stream and reset player state to prevent further decoding
				// mpeg_player_stop_audio() will reset start_time internally
				mpeg_player_stop_audio( player );
			}
		}
#endif
		AVI_CloseVideo( cin_state );
		cls.state = ca_disconnected;
		Key_SetKeyDest( key_menu );
#if !XASH_DREAMCAST
		S_StopStreaming();
#endif
		cls.movienum = -1;
		cin_time = 0.0f;
		cls.signon = 0;
		return;
	}

	// advances cinematic time (ignores maxfps and host_framerate settings)
	cin_time += host.realframetime;

	// stop the video after it finishes
	if( cin_time > video_duration + 0.1f )
	{
#if XASH_DREAMCAST
		// Ensure audio is stopped and video is closed before moving to next movie
		if( cin_state && AVI_IsActive( cin_state ))
		{
			mpeg_player_t *player = (mpeg_player_t *)AVI_GetMpegPlayer( cin_state );
			if( player )
				mpeg_player_stop_audio( player );
		}
#endif
		AVI_CloseVideo( cin_state );
		SCR_NextMovie( );
		return;
	}

#if XASH_DREAMCAST
	if( cin_state && AVI_IsActive( cin_state ))
	{
		mpeg_player_t *player = (mpeg_player_t *)AVI_GetMpegPlayer( cin_state );
		if( player )
		{
			// Poll audio and decode frames continuously
			mpeg_decode_result_t result = mpeg_decode_step( player );
			if( result == MPEG_DECODE_EOF )
			{
				// Video finished - ensure proper cleanup before moving to next movie
				mpeg_player_stop_audio( player );
				AVI_CloseVideo( cin_state );
				SCR_NextMovie( );
				return;
			}
		}
	}
#else
	// read the next frame
	cin_frame = AVI_GetVideoFrameNumber( cin_state, cin_time );
#endif
}

/*
==================
SCR_DrawCinematic

Returns true if a cinematic is active, meaning the view rendering
should be skipped
==================
*/
qboolean SCR_DrawCinematic( void )
{
#if XASH_DREAMCAST
	if( !ref.initialized || cin_time <= 0.0f )
		return false;

	mpeg_player_t *player = (mpeg_player_t *)AVI_GetMpegPlayer( cin_state );
	if( !player || !mpeg_player_get_frame( player ))
 		return false;

	// Upload and draw current frame (
	pvr_wait_ready();
	pvr_scene_begin();
	mpeg_upload_frame( player );
	pvr_list_begin( PVR_LIST_OP_POLY );
	mpeg_draw_frame( player );
	pvr_list_finish();
	pvr_scene_finish();
	
	return true;
#else
	static int	last_frame = -1;
	qboolean		redraw = false;
	byte		*frame = NULL;

	if( !ref.initialized || cin_time <= 0.0f )
		return false;

	if( cin_frame != last_frame )
	{
		frame = AVI_GetVideoFrame( cin_state, cin_frame );
		last_frame = cin_frame;
		redraw = true;
	}

	if( frame )
	{
		ref.dllFuncs.R_DrawStretchRaw( 0, 0, refState.width, refState.height, xres, yres, frame, redraw );
	}

	return true;
#endif
}

/*
==================
SCR_PlayCinematic
==================
*/
qboolean SCR_PlayCinematic( const char *arg )
{
	const char	*fullpath;

	fullpath = FS_GetDiskPath( arg, false );

	if( FS_FileExists( arg, false ) && !fullpath )
	{
		Con_Printf( S_ERROR "Couldn't load %s from packfile. Please extract it\n", arg );
		return false;
	}

	if( !fullpath )
	{
		Con_Printf( S_ERROR "SCR_PlayCinematic: file not found: %s\n", arg );
		return false;
	}

	// Stop any previous video before opening a new one
#if XASH_DREAMCAST
	if( cin_state && AVI_IsActive( cin_state ))
	{
		mpeg_player_t *player = (mpeg_player_t *)AVI_GetMpegPlayer( cin_state );
		if( player )
			mpeg_player_stop_audio( player );
	}
#endif
	AVI_CloseVideo( cin_state );

	AVI_OpenVideo( cin_state, fullpath, true, false );
	if( !AVI_IsActive( cin_state ))
	{
		Con_Printf( S_ERROR "SCR_PlayCinematic: AVI_OpenVideo failed for %s\n", fullpath );
		AVI_CloseVideo( cin_state );
		return false;
	}

	if( !( AVI_GetVideoInfo( cin_state, &xres, &yres, &video_duration ))) // couldn't open this at all.
	{
		AVI_CloseVideo( cin_state );
		return false;
	}

	if( AVI_GetAudioInfo( cin_state, &cin_audio ))
	{
		// begin streaming
		S_StopAllSounds( true );
#if XASH_DREAMCAST
		// On Dreamcast, MPEG player handles its own audio streaming
		// Audio was already started in AVI_OpenVideo
		// Don't call S_StartStreaming() as it expects SCR_GetAudioChunk to work
#else
		S_StartStreaming();
#endif
	}

	UI_SetActiveMenu( false );
	cls.state = ca_cinematic;
	Con_FastClose();
	cin_time = 0.0f;
	cls.signon = 0;

	return true;
}

int SCR_GetAudioChunk( char *rawdata, int length )
{
	int	r;

	r = AVI_GetAudioChunk( cin_state, rawdata, cin_audio.loopStart, length );
	cin_audio.loopStart += r; // advance play position

	return r;
}

wavdata_t *SCR_GetMovieInfo( void )
{
	if( AVI_IsActive( cin_state ))
		return &cin_audio;
	return NULL;
}

/*
==================
SCR_StopCinematic
==================
*/
void SCR_StopCinematic( void )
{
	if( cls.state != ca_cinematic )
		return;

	AVI_CloseVideo( cin_state );
#if XASH_DREAMCAST
	// MPEG player audio is stopped in AVI_CloseVideo
#else
	S_StopStreaming();
#endif
	cin_time = 0.0f;

	cls.state = ca_disconnected;
	cls.signon = 0;

	UI_SetActiveMenu( true );
}

/*
==================
SCR_InitCinematic
==================
*/
void SCR_InitCinematic( void )
{
	AVI_Initailize ();
	cin_state = AVI_GetState( CIN_MAIN );
}

/*
==================
SCR_FreeCinematic
==================
*/
void SCR_FreeCinematic( void )
{
	movie_state_t	*cin_state;

	// release videos
	cin_state = AVI_GetState( CIN_LOGO );
	AVI_CloseVideo( cin_state );

	cin_state = AVI_GetState( CIN_MAIN );
	AVI_CloseVideo( cin_state );

	AVI_Shutdown();
}
