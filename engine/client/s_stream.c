/*
s_stream.c - sound streaming
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
#include "sound.h"
#include "client.h"
#include "soundlib.h"
static bg_track_t		s_bgTrack;
static musicfade_t		musicfade;	// controlled by game dlls

void *music_callback(snd_stream_hnd_t hnd, int len, int *used)
{
    if (!s_bgTrack.stream)
	{
        *used = 0;
        return NULL;
    }

    if (len > MUSIC_BUFFER_SIZE) 
        len = MUSIC_BUFFER_SIZE;
    int r = FS_ReadStream(s_bgTrack.stream, len, music_buffer);

    if (r < len && s_bgTrack.loopName[0])
	{
        FS_FreeStream(s_bgTrack.stream);
        s_bgTrack.stream = FS_OpenStream(s_bgTrack.loopName);

        if (s_bgTrack.stream) 
		{
            Q_strncpy(s_bgTrack.current, s_bgTrack.loopName, sizeof(s_bgTrack.current));
            r = FS_ReadStream(s_bgTrack.stream, len, music_buffer);
        } else {
            r = 0;
        }
    }

    *used = r;
    return r > 0 ? music_buffer : NULL;
}
/*
=================
S_PrintBackgroundTrackState
=================
*/
void S_PrintBackgroundTrackState( void )
{
	Con_Printf( "BackgroundTrack: " );

	if( s_bgTrack.current[0] && s_bgTrack.loopName[0] )
		Con_Printf( "intro %s, loop %s\n", s_bgTrack.current, s_bgTrack.loopName );
	else if( s_bgTrack.current[0] )
		Con_Printf( "%s\n", s_bgTrack.current );
	else if( s_bgTrack.loopName[0] )
		Con_Printf( "%s [loop]\n", s_bgTrack.loopName );
	else Con_Printf( "not playing\n" );
}

/*
=================
S_FadeMusicVolume
=================
*/
void S_FadeMusicVolume( float fadePercent )
{
	musicfade.percent = bound( 0.0f, fadePercent, 100.0f );
}

/*
=================
S_GetMusicVolume
=================
*/
float S_GetMusicVolume( void )
{
	float	scale = 1.0f;

	if( host.status == HOST_NOFOCUS && snd_mute_losefocus.value != 0.0f )
	{
		// we return zero volume to keep sounds running
		return 0.0f;
	}

	if( !s_listener.inmenu && musicfade.percent != 0 )
	{
		scale = bound( 0.0f, musicfade.percent / 100.0f, 1.0f );
		scale = 1.0f - scale;
	}

	return s_musicvolume.value * scale;
}

/*
=================
S_StartBackgroundTrack
=================
*/
void S_StartBackgroundTrack(const char *introTrack, const char *mainTrack, int position, qboolean fullpath)
{
	S_StopBackgroundTrack();

	if( !dma.initialized ) return;

	// check for special symbols
	if( introTrack && *introTrack == '*' )
		introTrack = NULL;

	if( mainTrack && *mainTrack == '*' )
		mainTrack = NULL;

	if( !COM_CheckString( introTrack ) && !COM_CheckString( mainTrack ))
		return;

	if( !introTrack ) introTrack = mainTrack;
	if( !*introTrack ) return;

	if( !COM_CheckString( mainTrack ))
		s_bgTrack.loopName[0] = '\0';
	else Q_strncpy( s_bgTrack.loopName, mainTrack, sizeof( s_bgTrack.loopName ));

	// open stream
	s_bgTrack.stream = FS_OpenStream( introTrack );

	Q_strncpy( s_bgTrack.current, introTrack, sizeof( s_bgTrack.current ));
	memset( &musicfade, 0, sizeof( musicfade )); // clear any soundfade
	s_bgTrack.source = cls.key_dest;

	if( position != 0 )
	{
		// restore message, update song position
		FS_SetStreamPos( s_bgTrack.stream, position );
	}
#if XASH_DREAMCAST
    if (music_stream != SND_STREAM_INVALID) 
	{
        snd_stream_stop(music_stream);
        snd_stream_destroy(music_stream);
        music_stream = SND_STREAM_INVALID;
    }

    wavdata_t *info = FS_StreamInfo(s_bgTrack.stream);

    if (!info) 
	{
        Con_Printf("Failed to get stream info\n");
        FS_FreeStream(s_bgTrack.stream);
        s_bgTrack.stream = NULL;
        return;
    }

    if (info->width != 2) 
	{
        Con_Printf("Unsupported audio format: width=%d (expected 2 for 16-bit)\n", info->width);
        FS_FreeStream(s_bgTrack.stream);
        s_bgTrack.stream = NULL;
        return;
    }


    music_stream = snd_stream_alloc(music_callback, SND_STREAM_BUFFER_MAX_ADPCM);

    if (music_stream == SND_STREAM_INVALID) 
	{
        Con_Printf("S_StartBackgroundTrack: Stream allocation failed\n");
        FS_FreeStream(s_bgTrack.stream);
        s_bgTrack.stream = NULL;
        return;
    }

    snd_stream_set_userdata(music_stream, info);
    snd_stream_prefill(music_stream);
    snd_stream_start(music_stream, info->rate, info->channels - 1);
    snd_stream_volume(music_stream, (int)(s_musicvolume.value * 255));
    snd_stream_pan(music_stream, 128, 128);
#endif
}
/*
=================
S_StopBackgroundTrack
=================
*/
void S_StopBackgroundTrack( void )
{
	s_listener.stream_paused = false;

	if( !dma.initialized ) return;
	if( !s_bgTrack.stream ) return;

#if XASH_DREAMCAST
    if (music_stream != SND_STREAM_INVALID) 
	{
        snd_stream_stop(music_stream);
        snd_stream_destroy(music_stream);
        music_stream = SND_STREAM_INVALID;
    }
#endif


	FS_FreeStream( s_bgTrack.stream );
	memset( &s_bgTrack, 0, sizeof( bg_track_t ));
	memset( &musicfade, 0, sizeof( musicfade ));
}

/*
=================
S_StreamSetPause
=================
*/
void S_StreamSetPause( int pause )
{
	s_listener.stream_paused = pause;
#if XASH_DREAMCAST
	/* Actually stop/start the stream so music pauses during changelevel and resumes after signon. */
	if( music_stream == SND_STREAM_INVALID || !s_bgTrack.stream )
		return;
	if( pause )
		snd_stream_stop( music_stream );
	else
	{
		wavdata_t *info = FS_StreamInfo( s_bgTrack.stream );
		if( info )
			snd_stream_start( music_stream, info->rate, info->channels - 1 );
	}
#endif
}

/*
=================
S_StreamGetCurrentState

save\restore code
=================
*/
qboolean S_StreamGetCurrentState( char *currentTrack, size_t currentTrackSize, char *loopTrack, size_t loopTrackSize, int *position )
{
	if( !s_bgTrack.stream )
		return false; // not active

	if( currentTrack )
	{
		if( s_bgTrack.current[0] )
			Q_strncpy( currentTrack, s_bgTrack.current, currentTrackSize );
		else Q_strncpy( currentTrack, "*", currentTrackSize ); // no track
	}

	if( loopTrack )
	{
		if( s_bgTrack.loopName[0] )
			Q_strncpy( loopTrack, s_bgTrack.loopName, loopTrackSize );
		else Q_strncpy( loopTrack, "*", loopTrackSize ); // no track
	}

	if( position )
		*position = FS_GetStreamPos( s_bgTrack.stream );

	return true;
}

/*
=================
S_StreamBackgroundTrack
=================
*/
void S_StreamBackgroundTrack(void) 
{
#if XASH_DREAMCAST
	static double last_time = 0;
    double current_time = Sys_DoubleTime();

    if (!dma.initialized || !s_bgTrack.stream || s_listener.streaming || music_stream == SND_STREAM_INVALID) 
        return;
    
    float volume = S_GetMusicVolume();

	if( !s_musicvolume.value || s_listener.paused || s_listener.stream_paused )
		return;

	else if (!cl.background)
	{
        if ((s_bgTrack.source == key_game && cls.key_dest == key_menu) ||
            (s_bgTrack.source == key_menu && cls.key_dest != key_menu))
            volume = 0.0f;

    } else if (cls.key_dest == key_console)
        	volume = 0.0f;

    snd_stream_volume(music_stream, (int)(volume * 255));

    int poll_result = snd_stream_poll(music_stream);

    if (poll_result < 0) 
        S_StopBackgroundTrack();

    last_time = current_time;
#else
	int	bufferSamples;
	int	fileSamples;
	byte	raw[MAX_RAW_SAMPLES];
	int	r, fileBytes;
	rawchan_t	*ch = NULL;

	if( !dma.initialized || !s_bgTrack.stream || s_listener.streaming )
		return;

	// don't bother playing anything if musicvolume is 0
	if( !s_musicvolume.value || s_listener.paused || s_listener.stream_paused )
		return;

	if( !cl.background )
	{
		// pause music by source type
		if( s_bgTrack.source == key_game && cls.key_dest == key_menu ) return;
		if( s_bgTrack.source == key_menu && cls.key_dest != key_menu ) return;
	}
	else if( cls.key_dest == key_console )
		return;

	ch = S_FindRawChannel( S_RAW_SOUND_BACKGROUNDTRACK, true );

	Assert( ch != NULL );

	// see how many samples should be copied into the raw buffer
	if( ch->s_rawend < soundtime )
		ch->s_rawend = soundtime;

	while( ch->s_rawend < soundtime + ch->max_samples )
	{
		wavdata_t	*info = FS_StreamInfo( s_bgTrack.stream );

		bufferSamples = ch->max_samples - (ch->s_rawend - soundtime);

		// decide how much data needs to be read from the file
		fileSamples = bufferSamples * ((float)info->rate / SOUND_DMA_SPEED );
		if( fileSamples <= 1 ) return; // no more samples need

		// our max buffer size
		fileBytes = fileSamples * ( info->width * info->channels );

		if( fileBytes > sizeof( raw ))
		{
			fileBytes = sizeof( raw );
			fileSamples = fileBytes / ( info->width * info->channels );
		}

		// read
		r = FS_ReadStream( s_bgTrack.stream, fileBytes, raw );

		if( r < fileBytes )
		{
			fileBytes = r;
			fileSamples = r / ( info->width * info->channels );
		}

		if( r > 0 )
		{
			// add to raw buffer
			S_RawSamples( fileSamples, info->rate, info->width, info->channels, raw, S_RAW_SOUND_BACKGROUNDTRACK );
		}
		else
		{
			// loop
			if( s_bgTrack.loopName[0] )
			{
				FS_FreeStream( s_bgTrack.stream );
				s_bgTrack.stream = FS_OpenStream( s_bgTrack.loopName );
				Q_strncpy( s_bgTrack.current, s_bgTrack.loopName, sizeof( s_bgTrack.current ));

				if( !s_bgTrack.stream ) return;
			}
			else
			{
				S_StopBackgroundTrack();
				return;
			}
		}

	}
#endif
}

/*
=================
S_StartStreaming
=================
*/
void S_StartStreaming( void )
{
	if( !dma.initialized ) return;
	// begin streaming movie soundtrack
	s_listener.streaming = true;
}

/*
=================
S_StopStreaming
=================
*/
void S_StopStreaming( void )
{
	if( !dma.initialized ) return;
	s_listener.streaming = false;
}

/*
=================
S_StreamSoundTrack
=================
*/
void S_StreamSoundTrack( void )
{
	int	bufferSamples;
	int	fileSamples;
	byte	raw[MAX_RAW_SAMPLES];
	int	r, fileBytes;
	rawchan_t	*ch = NULL;

	if( !dma.initialized || !s_listener.streaming || s_listener.paused )
		return;

	ch = S_FindRawChannel( S_RAW_SOUND_SOUNDTRACK, true );

	Assert( ch != NULL );

	// see how many samples should be copied into the raw buffer
	if( ch->s_rawend < soundtime )
		ch->s_rawend = soundtime;

	while( ch->s_rawend < soundtime + ch->max_samples )
	{
		wavdata_t	*info = SCR_GetMovieInfo();

		if( !info ) break;	// bad soundtrack?

		bufferSamples = ch->max_samples - (ch->s_rawend - soundtime);

		// decide how much data needs to be read from the file
		fileSamples = bufferSamples * ((float)info->rate / SOUND_DMA_SPEED );
		if( fileSamples <= 1 ) return; // no more samples need

		// our max buffer size
		fileBytes = fileSamples * ( info->width * info->channels );

		if( fileBytes > sizeof( raw ))
		{
			fileBytes = sizeof( raw );
			fileSamples = fileBytes / ( info->width * info->channels );
		}

		// read audio stream
		r = SCR_GetAudioChunk( raw, fileBytes );

		if( r < fileBytes )
		{
			fileBytes = r;
			fileSamples = r / ( info->width * info->channels );
		}

		if( r > 0 )
		{
			// add to raw buffer
			S_RawSamples( fileSamples, info->rate, info->width, info->channels, raw, S_RAW_SOUND_SOUNDTRACK );
		}
		else break; // no more samples for this frame
	}
}
