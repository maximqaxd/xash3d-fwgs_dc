/*
mpg_dc.c - MPG video playback for Dreamcast
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

#include "build.h"
#if XASH_DREAMCAST
#include "common.h"
#include "client.h"
#include "avi.h"
#include <kos.h>
allocation
#define MPEG_MALLOC(sz)      Mem_Malloc( cls.mempool, sz )
#define MPEG_FREE(p)         Mem_Free( p )
#define MPEG_REALLOC(p, sz)  Mem_Realloc( cls.mempool, p, sz )
#define MPEG_MEMALIGN(a, sz) Mem_Malloc( cls.mempool, sz )  // Note: alignment not guaranteed, but should work
#define MPEG_MEMZERO(p, sz)  memset( p, 0, sz )

#include "../../ref/pvr/pvr_alloc.h"

#define MPEG_PVR_MALLOC(sz)  alloc_malloc( NULL, sz )
#define MPEG_PVR_FREE(p)     alloc_free( NULL, p )

#include "filesystem.h"
#define MPEG_FILE_TYPE                 dc_file_t
#define MPEG_FILE_INVALID_HANDLE      NULL
#define MPEG_FILE_OPEN(fn)             FS_Open( fn, "rb", false )
#define MPEG_FILE_CLOSE(fh)            FS_Close( fh )
#define MPEG_FILE_SEEK(fh, off, st)    FS_Seek( fh, off, st )
#define MPEG_FILE_READ(fh, buf, size)  FS_Read( fh, buf, size )
#define MPEG_FILE_TELL(fh)              FS_Tell( fh )

#include "pl_mpeg.h"

#include "mpeg.h"
typedef struct movie_state_s
{
	qboolean		active;
	qboolean		quiet;
	
	// MPG player
	mpeg_player_t	*mpeg_player;
	
	// Video info
	int		video_frames;	// total frames (estimated)
	int		video_xres;
	int		video_yres;
	float		video_fps;
	float		video_duration;
	
	// Audio info
	wavdata_t	audio_info;
	qboolean	has_audio;
	
	// Current frame state
	plm_frame_t	*current_frame;
	float		current_time;
	int		current_frame_num;
} movie_state_t;

static qboolean		avi_initialized = false;
static movie_state_t	avi[2];

qboolean AVI_GetVideoInfo( movie_state_t *Avi, int *xres, int *yres, float *duration )
{
	if( !Avi || !Avi->active || !Avi->mpeg_player )
		return false;
	
	if( xres ) *xres = Avi->video_xres;
	if( yres ) *yres = Avi->video_yres;
	if( duration ) *duration = Avi->video_duration;
	
	return true;
}

int AVI_GetVideoFrameNumber( movie_state_t *Avi, float time )
{
	if( !Avi || !Avi->active )
		return 0;
	
	// Calculate frame number from time and FPS
	return (int)(time * Avi->video_fps);
}

byte *AVI_GetVideoFrame( movie_state_t *Avi, int frame )
{
	if( !Avi || !Avi->active || !Avi->mpeg_player )
		return NULL;
	
	// If we need a different frame, decode it
	float frame_time = frame / Avi->video_fps;
	
	if( frame_time != Avi->current_time || frame != Avi->current_frame_num )
	{
		// Seek to the desired time and decode the frame
		if( !mpeg_player_seek( Avi->mpeg_player, frame_time, 0 ))
			return NULL;
		
		// Decode the frame
		plm_frame_t *decoded_frame = mpeg_player_decode_video( Avi->mpeg_player );
		if( !decoded_frame )
			return NULL;
		
		mpeg_player_set_frame( Avi->mpeg_player, decoded_frame );
		
		Avi->current_frame = decoded_frame;
		Avi->current_time = frame_time;
		Avi->current_frame_num = frame;
	}
	
	return (byte *)Avi->current_frame;
}

qboolean AVI_GetAudioInfo( movie_state_t *Avi, wavdata_t *snd_info )
{
	if( !Avi || !Avi->active || !snd_info )
		return false;
	
	if( !Avi->has_audio )
		return false;
	
	memcpy( snd_info, &Avi->audio_info, sizeof( wavdata_t ));
	return true;
}

int AVI_GetAudioChunk( movie_state_t *Avi, char *audiodata, int offset, int length )
{
	if( !Avi || !Avi->active || !Avi->has_audio || !audiodata )
		return 0;
	
	// MPG audio is handled by the streaming system, not frame-by-frame
	// This is a stub for compatibility
	return 0;
}

int AVI_TimeToSoundPosition( movie_state_t *Avi, int time )
{
	if( !Avi || !Avi->active || !Avi->has_audio )
		return 0;
	
	// Convert milliseconds to sample position
	return (time * Avi->audio_info.rate) / 1000 * Avi->audio_info.width * Avi->audio_info.channels;
}

void *AVI_GetMpegPlayer( movie_state_t *Avi )
{
	if( !Avi || !Avi->active )
		return NULL;
	return (void *)Avi->mpeg_player;
}

void AVI_OpenVideo( movie_state_t *Avi, const char *filename, qboolean load_audio, int quiet )
{
	char mpg_filename[MAX_OSPATH];
	const char *fullpath;
	
	if( !Avi )
		return;
	
	memset( Avi, 0, sizeof( movie_state_t ));
	Avi->quiet = quiet;
	
	// Check if filename is already an absolute path (starts with /)
	if( filename[0] == '/' )
	{
		// Already an absolute path - use it directly, but convert extension
		Q_strncpy( mpg_filename, filename, sizeof( mpg_filename ));
		COM_DefaultExtension( mpg_filename, ".mpg", sizeof( mpg_filename ));
		
		// Remove .avi if present
		char *dot = strstr( mpg_filename, ".avi" );
		if( dot )
			memmove( dot, dot + 4, strlen( dot + 4 ) + 1 );
		
		fullpath = mpg_filename;
	}
	else
	{
		// Relative path - convert .avi extension to .mpg
		Q_strncpy( mpg_filename, filename, sizeof( mpg_filename ));
		COM_DefaultExtension( mpg_filename, ".mpg", sizeof( mpg_filename ));
		
		// Remove .avi if present
		char *dot = strstr( mpg_filename, ".avi" );
		if( dot )
			memmove( dot, dot + 4, strlen( dot + 4 ) + 1 );
		
		fullpath = FS_GetDiskPath( mpg_filename, false );
		if( !fullpath )
		{
			if( !quiet )
				Con_DPrintf( S_ERROR "MPG file not found: %s\n", mpg_filename );
			return;
		}
	}
	
	// Create MPG player
	mpeg_player_options_t opts = MPEG_PLAYER_OPTIONS_INITIALIZER;
	opts.loop = false;
	opts.volume = 255;
	
	Avi->mpeg_player = mpeg_player_create_ex( fullpath, &opts );
	if( !Avi->mpeg_player )
	{
		if( !quiet )
			Con_DPrintf( S_ERROR "Failed to create MPG player for: %s\n", fullpath );
		return;
	}
	
		// Get video info
		Avi->video_xres = mpeg_player_get_width( Avi->mpeg_player );
		Avi->video_yres = mpeg_player_get_height( Avi->mpeg_player );
		Avi->video_fps = (float)mpeg_player_get_framerate( Avi->mpeg_player );
		Avi->video_duration = (float)mpeg_player_get_duration( Avi->mpeg_player );
		Avi->video_frames = (int)(Avi->video_duration * Avi->video_fps);
		
		// Get audio info if available
		if( load_audio && mpeg_player_get_num_audio_streams( Avi->mpeg_player ) > 0 )
		{
			Avi->has_audio = true;
			Avi->audio_info.rate = mpeg_player_get_samplerate( Avi->mpeg_player );
			Avi->audio_info.channels = mpeg_player_get_num_audio_streams( Avi->mpeg_player );
			Avi->audio_info.width = 2; // 16-bit
			Avi->audio_info.type = WF_PCMDATA;
			
			// Start audio streaming
			mpeg_player_start_audio( Avi->mpeg_player );
		}
	
	Avi->active = true;
	Avi->current_time = -1.0f;
	Avi->current_frame_num = -1;
}

void AVI_CloseVideo( movie_state_t *Avi )
{
	if( !Avi )
		return;
	
	if( Avi->mpeg_player )
	{
		// Stop audio before destroying
		mpeg_player_stop_audio( Avi->mpeg_player );
		mpeg_player_destroy( Avi->mpeg_player );
		Avi->mpeg_player = NULL;
	}
	
	// No frame buffer needed - using YUV rendering directly
	
	memset( Avi, 0, sizeof( movie_state_t ));
}

qboolean AVI_IsActive( movie_state_t *Avi )
{
	if( Avi != NULL )
		return Avi->active;
	return false;
}

movie_state_t *AVI_GetState( int num )
{
	if( num >= 0 && num < 2 )
		return &avi[num];
	return NULL;
}

movie_state_t *AVI_LoadVideo( const char *filename, qboolean load_audio )
{
	movie_state_t	*Avi;
	string		path;
	const char	*fullpath;
	
	// fast reject
	if( !avi_initialized )
		return NULL;
	
	// open cinematic
	Q_snprintf( path, sizeof( path ), "media/%s", filename );
	COM_DefaultExtension( path, ".mpg", sizeof( path ));
	fullpath = FS_GetDiskPath( path, false );
	
	if( FS_FileExists( path, false ) && !fullpath )
	{
		Con_Printf( "Couldn't load %s from packfile. Please extract it\n", path );
		return NULL;
	}
	
	Avi = Mem_Malloc( cls.mempool, sizeof( movie_state_t ));
	AVI_OpenVideo( Avi, fullpath, load_audio, false );
	
	if( !AVI_IsActive( Avi ))
	{
		AVI_FreeVideo( Avi );
		return NULL;
	}
	
	return Avi;
}

void AVI_FreeVideo( movie_state_t *state )
{
	if( !state ) return;
	
	if( Mem_IsAllocatedExt( cls.mempool, state ))
	{
		AVI_CloseVideo( state );
		Mem_Free( state );
	}
}

qboolean AVI_Initailize( void )
{
	if( avi_initialized )
		return true;
	
	avi_initialized = true;
	memset( avi, 0, sizeof( avi ));
	
	return true;
}

void AVI_Shutdown( void )
{
	int i;
	
	if( !avi_initialized )
		return;
	
	for( i = 0; i < 2; i++ )
		AVI_CloseVideo( &avi[i] );
	
	avi_initialized = false;
}

#endif // XASH_DREAMCAST
