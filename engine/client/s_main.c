/*
s_main.c - sound engine
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
#include "con_nprint.h"
#include "pm_local.h"
#include "platform/platform.h"

#define SND_CLIP_DISTANCE		1000.0f

dma_t		dma;
poolhandle_t sndpool;
static soundfade_t	soundfade;
channel_t   	channels[MAX_CHANNELS];
sound_t		ambient_sfx[NUM_AMBIENTS];
rawchan_t		*raw_channels[MAX_RAW_CHANNELS];
qboolean		snd_ambient = false;
qboolean		snd_fade_sequence = false;
listener_t	s_listener;
int		total_channels;
int		soundtime;	// sample PAIRS
int   		paintedtime; 	// sample PAIRS

static CVAR_DEFINE( s_volume, "volume", "0.7", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "sound volume" );
CVAR_DEFINE( s_musicvolume, "MP3Volume", "1.0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "background music volume" );
static CVAR_DEFINE( s_mixahead, "_snd_mixahead", "0.12", FCVAR_FILTERABLE, "how much sound to mix ahead of time" );
static CVAR_DEFINE_AUTO( s_show, "0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "show playing sounds" );
CVAR_DEFINE_AUTO( s_lerping, "0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "apply interpolation to sound output" );
static CVAR_DEFINE( s_ambient_level, "ambient_level", "0.3", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "volume of environment noises (water and wind)" );
static CVAR_DEFINE( s_ambient_fade, "ambient_fade", "1000", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "rate of volume fading when client is moving" );
static CVAR_DEFINE_AUTO( s_combine_sounds, "0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "combine channels with same sounds" );
CVAR_DEFINE_AUTO( snd_mute_losefocus, "1", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "silence the audio when game window loses focus" );
CVAR_DEFINE_AUTO( s_test, "0", 0, "engine developer cvar for quick testing new features" );
CVAR_DEFINE_AUTO( s_samplecount, "0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "sample count (0 for default value)" );
CVAR_DEFINE_AUTO( s_warn_late_precache, "0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "warn about late precached sounds on client-side" );

static qboolean aica_channels_in_use[64] = { false };  // Track AICA channel usage


/*
=============================================================================

		SOUNDS PROCESSING

=============================================================================
*/
/*
=================
S_GetMasterVolume
=================
*/
float S_GetMasterVolume( void )
{
	float	scale = 1.0f;

	if( host.status == HOST_NOFOCUS && snd_mute_losefocus.value != 0.0f )
	{
		// we return zero volume to keep sounds running
		return 0.0f;
	}

	if( !s_listener.inmenu && soundfade.percent != 0 )
	{
		scale = bound( 0.0f, soundfade.percent / 100.0f, 1.0f );
		scale = 1.0f - scale;
	}
	return s_volume.value * scale;
}

/*
=================
S_FadeClientVolume
=================
*/
void S_FadeClientVolume( float fadePercent, float fadeOutSeconds, float holdTime, float fadeInSeconds )
{
	soundfade.starttime	= cl.mtime[0];
	soundfade.initial_percent = fadePercent;
	soundfade.fadeouttime = fadeOutSeconds;
	soundfade.holdtime = holdTime;
	soundfade.fadeintime = fadeInSeconds;
}

/*
=================
S_IsClient
=================
*/
static qboolean S_IsClient( int entnum )
{
	return ( entnum == s_listener.entnum );
}


// free channel so that it may be allocated by the
// next request to play a sound.  If sound is a
// word in a sentence, release the sentence.
// Works for static, dynamic, sentence and stream sounds
/*
=================
S_FreeChannel
=================
*/
void S_FreeChannel( channel_t *ch )
{
#ifdef XASH_DREAMCAST
#if 0
    Con_DPrintf("AICA: Attempting to free channel %d (static: %s, sfx: %s)\n", 
        ch->aica_channel,
        ch->aica_channel >= MAX_DYNAMIC_CHANNELS ? "yes" : "no",
        ch->sfx ? ch->sfx->name : "null");
#endif

    // Don't protect static channels from being freed
    if (ch->aica_channel < MAX_DYNAMIC_CHANNELS && 
        Sys_DoubleTime() - ch->start_time < 0.1)  // 100ms protection for dynamic only
    {
#if 0
        Con_DPrintf("AICA: Skipping free of recently allocated channel %d\n", ch->aica_channel);
#endif
        return;
    }

    ch->sfx = NULL;
    ch->active = false;
    ch->use_loop = false;
    ch->start_time = 0;  // Reset start time when freeing
#if 0	

    Con_DPrintf("AICA: Successfully freed channel %d\n", ch->aica_channel);
#endif
#endif

    ch->sfx = NULL;
    ch->name[0] = '\0';
    ch->use_loop = false;
    ch->isSentence = false;

    // clear mixer
    memset( &ch->pMixer, 0, sizeof( ch->pMixer ));

    SND_CloseMouth( ch );
}
/*
=================
S_UpdateSoundFade
=================
*/
static void S_UpdateSoundFade( void )
{
	float	f, totaltime, elapsed;

	// determine current fade value.
	// assume no fading remains
	soundfade.percent = 0;

	totaltime = soundfade.fadeouttime + soundfade.fadeintime + soundfade.holdtime;

	elapsed = cl.mtime[0] - soundfade.starttime;

	// clock wrapped or reset (BUG) or we've gone far enough
	if( elapsed < 0.0f || elapsed >= totaltime || totaltime <= 0.0f )
		return;

	// We are in the fade time, so determine amount of fade.
	if( soundfade.fadeouttime > 0.0f && ( elapsed < soundfade.fadeouttime ))
	{
		// ramp up
		f = elapsed / soundfade.fadeouttime;
	}
	else if( elapsed <= ( soundfade.fadeouttime + soundfade.holdtime ))	// Inside the hold time
	{
		// stay
		f = 1.0f;
	}
	else
	{
		// ramp down
		f = ( elapsed - ( soundfade.fadeouttime + soundfade.holdtime ) ) / soundfade.fadeintime;
		f = 1.0f - f; // backward interpolated...
	}

	// spline it.
	f = -( cos( M_PI * f ) - 1 ) / 2;
	f = bound( 0.0f, f, 1.0f );

	soundfade.percent = soundfade.initial_percent * f;

	if( snd_fade_sequence )
		S_FadeMusicVolume( soundfade.percent );

	if( snd_fade_sequence && soundfade.percent == 100.0f )
	{
		S_StopAllSounds( false );
		S_StopBackgroundTrack();
		snd_fade_sequence = false;
	}
}

/*
=================
SND_FStreamIsPlaying

Select a channel from the dynamic channel allocation area.  For the given entity,
override any other sound playing on the same channel (see code comments below for
exceptions).
=================
*/
static qboolean SND_FStreamIsPlaying( sfx_t *sfx )
{
	int	ch_idx;

	for( ch_idx = NUM_AMBIENTS; ch_idx < MAX_DYNAMIC_CHANNELS; ch_idx++ )
	{
		if( channels[ch_idx].sfx == sfx )
			return true;
	}

	return false;
}

/*
=================
SND_GetChannelTimeLeft

TODO: this function needs to be removed after whole sound subsystem rewrite
=================
*/
static int SND_GetChannelTimeLeft(const channel_t *ch)
{
#ifdef XASH_DREAMCAST
    if (!ch->active || !ch->sfx || !ch->sfx->cache)
        return 0;
    
    uint32_t samples = ch->sfx->cache->samples;
    double current_time = Sys_DoubleTime();
    
    // For static channels
    if (ch->aica_channel >= MAX_DYNAMIC_CHANNELS)
    {
        // If start time is invalid, initialize it
        if (ch->start_time <= 0) {
#if 0
            Con_DPrintf("AICA: Static channel %d has invalid start time, skipping time check\n", 
                ch->aica_channel);
#endif
            return samples;  // Give it full duration
        }
        
        double elapsed = current_time - ch->start_time;
        uint32_t curpos = (uint32_t)(elapsed * ch->sfx->cache->rate);
        uint32_t remaining = (curpos >= samples) ? 0 : (samples - curpos);
        
#if 0
        // Only log status for active static channels
        Con_DPrintf("AICA: Static channel %d status - played: %u/%u (%.1f%%), age: %.2fs\n", 
            ch->aica_channel, 
            curpos,
            samples,
            (float)curpos / samples * 100.0f,
            elapsed);
#endif
        // More conservative freeing for static channels
        if (elapsed >= 0.5) {  // 500ms minimum lifetime
            if (remaining < (samples * 0.1))  // 90% played instead of 80%
            {
#if 0
                Con_DPrintf("AICA: Static channel %d is near end (remaining: %u/%u)\n", 
                    ch->aica_channel, remaining, samples);
#endif
                return 0;
            }
        }
        return remaining;
    }
    
    // For dynamic channels
    double elapsed = current_time - ch->start_time;
    if (elapsed < 0.1)  // 100ms protection for dynamic channels
        return samples;
        
    uint32_t curpos = (uint32_t)(elapsed * ch->sfx->cache->rate);
    uint32_t remaining = (curpos >= samples) ? 0 : (samples - curpos);
    
    // For looped sounds, maintain higher priority
    if (ch->use_loop)
        return remaining;
    
    return remaining;
#else
    int remaining;

    if( ch->pMixer.finished || !ch->sfx || !ch->sfx->cache || !ch->active )
        return 0;

    if( ch->isSentence )
    {
        // ... existing sentence code ...
        return 0;
    }
    else
    {
        int curpos;
        int samples;
        samples = ch->sfx->cache->samples;
        curpos = S_ConvertLoopedPosition( ch->sfx->cache, ch->pMixer.sample, ch->use_loop );
        remaining = bound( 0, samples - curpos, samples );
    }
    return remaining;
#endif
}

/*
=================
SND_PickDynamicChannel

Select a channel from the dynamic channel allocation area.  For the given entity,
override any other sound playing on the same channel (see code comments below for
exceptions).
=================
*/
channel_t *SND_PickDynamicChannel( int entnum, int channel, sfx_t *sfx, qboolean *ignore )
{
    int ch_idx;
    int first_to_die;
    int life_left;
    int timeleft;

    // check for replacement sound, or find the best one to replace
    first_to_die = -1;
    life_left = 0x7fffffff;
    if( ignore ) *ignore = false;

    if( channel == CHAN_STREAM && SND_FStreamIsPlaying( sfx ))
    {
        if( ignore )
            *ignore = true;
        return NULL;
    }
    for( ch_idx = NUM_AMBIENTS; ch_idx < MAX_DYNAMIC_CHANNELS; ch_idx++ )
    {
        channel_t *ch = &channels[ch_idx];

        // Never override a streaming sound that is currently playing or
        // voice over IP data that is playing or any sound on CHAN_VOICE( acting )
        if( ch->sfx && ( ch->entchannel == CHAN_STREAM ))
            continue;

        if( channel != CHAN_AUTO && ch->entnum == entnum && ( ch->entchannel == channel || channel == -1 ))
        {
            // always override sound from same entity
            first_to_die = ch_idx;
            break;
        }


        // don't let monster sounds override player sounds
        if( ch->sfx && S_IsClient( ch->entnum ) && !S_IsClient( entnum ))
            continue;

        // try to pick the sound with the least amount of data left to play
        timeleft = SND_GetChannelTimeLeft( ch );

        if( timeleft < life_left )
        {
            life_left = timeleft;
            first_to_die = ch_idx;
        }
    }


    if( first_to_die == -1 )
        return NULL;

    if( channels[first_to_die].sfx )
    {
        // don't restart looping sounds for the same entity
        wavdata_t *sc = channels[first_to_die].sfx->cache;

        if( sc && FBitSet( sc->flags, SOUND_LOOPED ))
        {
            channel_t *ch = &channels[first_to_die];

            if( ch->entnum == entnum && ch->entchannel == channel && ch->sfx == sfx )
            {
                if( ignore ) *ignore = true;
                // same looping sound, same ent, same channel, don't restart the sound
                return NULL;
            }
        }

#ifdef XASH_DREAMCAST
        // Stop AICA playback before freeing the channel
        channel_t *ch = &channels[first_to_die];
        if(ch->active)
        {
            AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
            cmd->cmd = AICA_CMD_CHAN;
            cmd->timestamp = 0;
            cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
            cmd->cmd_id = ch->aica_channel;
            chan->cmd = AICA_CH_CMD_STOP;
            snd_sh4_to_aica(tmp, cmd->size);
            aica_channels_in_use[ch->aica_channel] = false;
            ch->active = false;
        }
#endif
        // be sure and release previous channel if sentence.
        S_FreeChannel( &( channels[first_to_die] ));
    }

    channel_t *ch = &channels[first_to_die];
#ifdef XASH_DREAMCAST
    ch->aica_channel = first_to_die;
    aica_channels_in_use[first_to_die] = true;
    ch->active = true;
#endif

    return ch;
}

/*
=====================
SND_PickStaticChannel

Pick an empty channel from the static sound area, or allocate a new
channel.  Only fails if we're at max_channels (128!!!) or if
we're trying to allocate a channel for a stream sound that is
already playing.
=====================
*/

channel_t *SND_PickStaticChannel( const vec3_t pos, sfx_t *sfx )
{
    channel_t *ch = NULL;
    int i;
    int free_count = 0;

    // Try to reclaim finished channels first
    for( i = MAX_DYNAMIC_CHANNELS; i < total_channels; i++ )
    {
        if( channels[i].sfx && channels[i].active )
        {
            // Check if sound has finished playing
            if( SND_GetChannelTimeLeft( &channels[i] ) <= 0 )
            {
#if 0	
                Con_DPrintf("AICA: Reclaiming finished channel %d\n", i);
#endif
                S_FreeChannel( &channels[i] );
            }
        }
    }

    // Count truly free channels
    for( i = MAX_DYNAMIC_CHANNELS; i < total_channels; i++ )
    {
        if( channels[i].sfx == NULL && !channels[i].active )
            free_count++;
    }
#if 0	
    Con_DPrintf("Static channels status: %d free of %d total (dynamic max: %d)\n", 
        free_count, 
        total_channels - MAX_DYNAMIC_CHANNELS,
        MAX_DYNAMIC_CHANNELS);
#endif
    // check for replacement sound, or find the best one to replace
    for( i = MAX_DYNAMIC_CHANNELS; i < total_channels; i++ )
    {
        // Channel is only truly free if both sfx is NULL and not active
        if( channels[i].sfx == NULL && !channels[i].active )
        {
#if 0	
            Con_DPrintf("Found free static channel at %d\n", i);
#endif
            break;
        }

        if( VectorCompare( pos, channels[i].origin ) && channels[i].sfx == sfx )
        {
#if 0	
            Con_DPrintf("Found matching static sound at %d\n", i);
#endif
            break;
        }
    }

    if( i < total_channels )
    {
        // reuse an empty static sound channel
        ch = &channels[i];
#ifdef XASH_DREAMCAST
        ch->aica_channel = i;
        aica_channels_in_use[i] = true;
        ch->active = true;
#if 0
        Con_DPrintf("AICA: Reusing static channel %d\n", i);
#endif
#endif
    }
    else
    {
        // no empty slots, alloc a new static sound channel
        if( total_channels == MAX_CHANNELS )
        {
            Con_DPrintf( S_ERROR "%s: no free channels (total: %d, max: %d)\n", 
                __func__, total_channels, MAX_CHANNELS );
            return NULL;
        }

        // get a channel for the static sound
        ch = &channels[total_channels];
#ifdef XASH_DREAMCAST
        ch->aica_channel = total_channels;
        aica_channels_in_use[total_channels] = true;
        ch->active = true;
#if 0		
        Con_DPrintf("AICA: Allocated new static channel %d\n", total_channels);	
#endif
#endif
        total_channels++;
    }
    return ch;
}


/*
=================
S_AlterChannel

search through all channels for a channel that matches this
soundsource, entchannel and sfx, and perform alteration on channel
as indicated by 'flags' parameter. If shut down request and
sfx contains a sentence name, shut off the sentence.
returns TRUE if sound was altered,
returns FALSE if sound was not found (sound is not playing)
=================
*/
static int S_AlterChannel( int entnum, int channel, sfx_t *sfx, int vol, int pitch, int flags )
{
    channel_t *ch;
    int i;

    if( S_TestSoundChar( sfx->name, '!' ))
    {
        // This is a sentence name
        for( i = NUM_AMBIENTS, ch = channels + NUM_AMBIENTS; i < total_channels; i++, ch++ )
        {
            if( ch->entnum == entnum && ch->entchannel == channel && ch->sfx && ch->isSentence )
            {
#ifdef XASH_DREAMCAST
                if( flags & SND_CHANGE_VOL && ch->active )
                {
                    // Update AICA volume
                    AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
                    cmd->cmd = AICA_CMD_CHAN;
                    cmd->timestamp = 0;
                    cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
                    cmd->cmd_id = ch->aica_channel;
                    chan->cmd = AICA_CH_CMD_UPDATE;
                    chan->vol = vol;  // Might need scaling here
                    snd_sh4_to_aica(tmp, cmd->size);
                }
#endif
                if( flags & SND_CHANGE_PITCH )
                    ch->basePitch = pitch;

                if( flags & SND_CHANGE_VOL )
                    ch->master_vol = vol;

                if( flags & SND_STOP )
                    S_FreeChannel( ch );

                return true;
            }
        }
        return false;
    }

    // regular sound or streaming sound
    for( i = NUM_AMBIENTS, ch = channels + NUM_AMBIENTS; i < total_channels; i++, ch++ )
    {
        if( ch->entnum == entnum && ch->entchannel == channel && ch->sfx == sfx )
        {
#ifdef XASH_DREAMCAST
            if( flags & SND_CHANGE_VOL && ch->active )
            {
                // Update AICA volume
                AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
                cmd->cmd = AICA_CMD_CHAN;
                cmd->timestamp = 0;
                cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
                cmd->cmd_id = ch->aica_channel;
                chan->cmd = AICA_CH_CMD_UPDATE;
                chan->vol = vol;  // Might need scaling here
                snd_sh4_to_aica(tmp, cmd->size);
            }
#endif
            if( flags & SND_CHANGE_PITCH )
                ch->basePitch = pitch;

            if( flags & SND_CHANGE_VOL )
                ch->master_vol = vol;

            if( flags & SND_STOP )
                S_FreeChannel( ch );

            return true;
        }
    }
    return false;
}

/*
=================
S_SpatializeChannel
=================
*/
static void S_SpatializeChannel( int *left_vol, int *right_vol, int master_vol, float gain, float dot, float dist, int *pan )
{
    float lscale, rscale, scale;

    rscale = 1.0f + dot;
    lscale = 1.0f - dot;

    // add in distance effect
    scale = ( 1.0f - dist ) * rscale;
    *right_vol = (int)( master_vol * scale );

    scale = ( 1.0f - dist ) * lscale;
    *left_vol = (int)( master_vol * scale );
	

#ifdef XASH_DREAMCAST
    // AICA uses 0-255 for volume and pan
    // Convert volumes to AICA format
 	*right_vol = bound( 0, *right_vol * 0.65f, 255 );
    *left_vol = bound( 0, *left_vol * 0.65f, 255 );

     // Calculate pan based on volume difference
    if (*right_vol == *left_vol)
        *pan = 128;  // Center
    else if (*right_vol > *left_vol)
        *pan = 128 + ((*right_vol - *left_vol) * 127 / 255);
    else
        *pan = 128 - ((*left_vol - *right_vol) * 128 / 255);

    // Use average of volumes instead of maximum
    int avg_vol = (*right_vol + *left_vol) / 2;
    *right_vol = *left_vol = avg_vol;
#else
    *right_vol = bound( 0, *right_vol, 255 );
    *left_vol = bound( 0, *left_vol, 255 );
    *pan = 128;  // Default center pan for non-DC platforms
#endif
}

/*
=================
SND_Spatialize
=================
*/
static void SND_Spatialize( channel_t *ch )
{
    vec3_t    source_vec;
    float    dist, dot, gain = 1.0f;
    qboolean    looping = false;
    wavdata_t    *pSource;
    int pan = 128;  // Default center pan

    // anything coming from the view entity will always be full volume
    if( S_IsClient( ch->entnum ))
    {
        ch->leftvol = ch->master_vol;
        ch->rightvol = ch->master_vol;
        return;
    }

    pSource = ch->sfx->cache;

    if( ch->use_loop && pSource && FBitSet( pSource->flags, SOUND_LOOPED ))
        looping = true;

    if( !ch->staticsound )
    {
        if( !CL_GetEntitySpatialization( ch ))
        {
            // origin is null and entity not exist on client
            ch->leftvol = ch->rightvol = 0;
            return;
        }
    }

    // source_vec is vector from listener to sound source
    VectorSubtract( ch->origin, s_listener.origin, source_vec );

    // normalize source_vec and get distance from listener to source
    dist = VectorNormalizeLength( source_vec );
    dot = DotProduct( s_listener.right, source_vec );

    if( !FBitSet( host.bugcomp, BUGCOMP_SPATIALIZE_SOUND_WITH_ATTN_NONE ))
    {
        // don't pan sounds with no attenuation
        if( ch->dist_mult <= 0.0f ) dot = 0.0f;
    }

#ifdef XASH_DREAMCAST
     // Calculate spatialization for AICA
    S_SpatializeChannel( &ch->leftvol, &ch->rightvol, ch->master_vol, gain, dot, dist * ch->dist_mult, &pan );

    // Only update AICA if the channel is already playing
    if(ch->active && ch->sfx && ch->sfx->cache)
    {
        AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
        
        cmd->cmd = AICA_CMD_CHAN;
        cmd->timestamp = 0;
        cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
        cmd->cmd_id = ch->aica_channel;
        
        chan->cmd = AICA_CH_CMD_UPDATE;
        chan->vol = ch->leftvol;
        chan->pan = pan;
        
        snd_sh4_to_aica(tmp, cmd->size);
    }

#else
    // Original spatialization
    S_SpatializeChannel( &ch->leftvol, &ch->rightvol, ch->master_vol, gain, dot, dist * ch->dist_mult, &pan );

    // if playing a word, set volume
    VOX_SetChanVol( ch );
#endif
}
/*
====================
S_StartSound

Start a sound effect for the given entity on the given channel (ie; voice, weapon etc).
Try to grab a channel out of the 8 dynamic spots available.
Currently used for looping sounds, streaming sounds, sentences, and regular entity sounds.
NOTE: volume is 0.0 - 1.0 and attenuation is 0.0 - 1.0 when passed in.
Pitch changes playback pitch of wave by % above or below 100.  Ignored if pitch == 100

NOTE: it's not a good idea to play looping sounds through StartDynamicSound, because
if the looping sound starts out of range, or is bumped from the buffer by another sound
it will never be restarted.  Use StartStaticSound (pass CHAN_STATIC to EMIT_SOUND or
SV_StartSound.
====================
*/
void S_StartSound( const vec3_t pos, int ent, int chan, sound_t handle, float fvol, float attn, int pitch, int flags )
{
    wavdata_t    *pSource;
    sfx_t    *sfx = NULL;
    channel_t    *target_chan, *check;
    int    vol, ch_idx;
    qboolean    bIgnore = false;

    if( !dma.initialized ) return;
    sfx = S_GetSfxByHandle( handle );
    if( !sfx ) return;

    vol = bound( 0, fvol * 255, 255 );

    if( pitch <= 1 ) pitch = PITCH_NORM; // Invasion issues

    if( flags & ( SND_STOP|SND_CHANGE_VOL|SND_CHANGE_PITCH ))
    {
        if( S_AlterChannel( ent, chan, sfx, vol, pitch, flags ))
            return;

        if( flags & SND_STOP ) return;
    }

    if( !pos ) pos = refState.vieworg;

    if( chan == CHAN_STREAM )
        SetBits( flags, SND_STOP_LOOPING );

    // pick a channel to play on
    if( chan == CHAN_STATIC ) target_chan = SND_PickStaticChannel( pos, sfx );
    else target_chan = SND_PickDynamicChannel( ent, chan, sfx, &bIgnore );

    if( !target_chan )
    {
        if( !bIgnore )
            Con_DPrintf( S_ERROR "dropped sound \"" DEFAULT_SOUNDPATH "%s\"\n", sfx->name );
        return;
    }
#ifdef XASH_DREAMCAST
    // Store AICA channel and state before memset
    int saved_aica_channel = target_chan->aica_channel;
    qboolean was_active = target_chan->active;
    qboolean was_in_use = aica_channels_in_use[saved_aica_channel];
#if 0
    Con_DPrintf("AICA: Saving channel state %d (active: %s, in_use: %s)\n",
        saved_aica_channel,
        was_active ? "yes" : "no",
        was_in_use ? "yes" : "no");
#endif
#endif

    // spatialize
    memset( target_chan, 0, sizeof( *target_chan ));

#ifdef XASH_DREAMCAST
    // Restore AICA state
    target_chan->aica_channel = saved_aica_channel;
    aica_channels_in_use[saved_aica_channel] = was_in_use;
    
    // If channel was active, stop it first
    if(was_active)
    {
#if 0
        Con_DPrintf("AICA: Stopping active channel %d before reuse\n", saved_aica_channel);
#endif
        AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
        cmd->cmd = AICA_CMD_CHAN;
        cmd->timestamp = 0;
        cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
        cmd->cmd_id = target_chan->aica_channel;
        chan->cmd = AICA_CH_CMD_STOP;
        snd_sh4_to_aica(tmp, cmd->size);
    }
#endif

    VectorCopy( pos, target_chan->origin );
    target_chan->staticsound = ( ent == 0 ) ? true : false;
    target_chan->use_loop = (flags & SND_STOP_LOOPING) ? false : true;
    target_chan->localsound = (flags & SND_LOCALSOUND) ? true : false;
    target_chan->dist_mult = (attn / SND_CLIP_DISTANCE);
    target_chan->master_vol = vol;
    target_chan->entnum = ent;
    target_chan->entchannel = chan;
    target_chan->basePitch = pitch;
    target_chan->isSentence = false;
    target_chan->sfx = sfx;

    pSource = S_LoadSound( sfx );
    
    if( !pSource )
    {
        S_FreeChannel( target_chan );
        return;
    }

    SND_Spatialize( target_chan );

    // If a client can't hear a sound when they FIRST receive the StartSound message,
    // the client will never be able to hear that sound.
    if( !target_chan->leftvol && !target_chan->rightvol )
    {
        if( !sfx->cache || !FBitSet( sfx->cache->flags, SOUND_LOOPED ))
        {
            if( chan != CHAN_STREAM )
            {
                S_FreeChannel( target_chan );
                return; // not audible at all
            }
        }
    }

#ifdef XASH_DREAMCAST
    // Start sound on AICA
    if(pSource)
    {
        qboolean should_loop = (target_chan->use_loop && FBitSet(pSource->flags, SOUND_LOOPED));
        
        AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
        
        cmd->cmd = AICA_CMD_CHAN;
        cmd->timestamp = 0;
        cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
        cmd->cmd_id = target_chan->aica_channel;
        
        chan->cmd = AICA_CH_CMD_START;
        chan->base = pSource->aica_pos;
        chan->type = pSource->type;
        chan->length = pSource->size;
        
        chan->loop = should_loop;
        chan->loopstart = 0;
        chan->loopend = pSource->size;
        
        chan->freq = pSource->rate;
        chan->vol = (target_chan->leftvol + target_chan->rightvol) / 2;
        chan->pan = 128;  // Will be updated by spatialize
        
        snd_sh4_to_aica(tmp, cmd->size);
        
        aica_channels_in_use[target_chan->aica_channel] = true;
        target_chan->active = true;
        target_chan->start_time = Sys_DoubleTime();
    }
#endif

    // Init client entity mouth movement vars
    SND_InitMouth( ent, chan );
}
/*
====================
S_RestoreSound

Restore a sound effect for the given entity on the given channel
====================
*/
void S_RestoreSound(const vec3_t pos, int ent, int chan, sound_t handle, float fvol, float attn, int pitch, int flags, double sample, double end, int wordIndex)
{
    wavdata_t    *pSource;
    sfx_t    *sfx = NULL;
    channel_t    *target_chan;
    qboolean    bIgnore = false;
    int    vol;

    if(!dma.initialized) return;
    sfx = S_GetSfxByHandle(handle);
    if(!sfx) return;

    vol = bound(0, fvol * 255, 255);
    if(pitch <= 1) pitch = PITCH_NORM; // Invasion issues

    // pick a channel to play on
    if(chan == CHAN_STATIC) target_chan = SND_PickStaticChannel(pos, sfx);
    else target_chan = SND_PickDynamicChannel(ent, chan, sfx, &bIgnore);

    if(!target_chan)
    {
        if(!bIgnore)
            Con_DPrintf(S_ERROR "dropped sound \"" DEFAULT_SOUNDPATH "%s\"\n", sfx->name);
        return;
    }

#ifdef XASH_DREAMCAST
    // Store AICA channel state before memset
    int saved_aica_channel = target_chan->aica_channel;
    qboolean was_active = target_chan->active;
    
    // Stop previous sound if active
    if(was_active)
    {
        AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
        cmd->cmd = AICA_CMD_CHAN;
        cmd->timestamp = 0;
        cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
        cmd->cmd_id = saved_aica_channel;
        chan->cmd = AICA_CH_CMD_STOP;
        snd_sh4_to_aica(tmp, cmd->size);
    }
#endif

    // spatialize
    memset(target_chan, 0, sizeof(*target_chan));

#ifdef XASH_DREAMCAST
    // Restore AICA channel
    target_chan->aica_channel = saved_aica_channel;
#endif

    VectorCopy(pos, target_chan->origin);
    target_chan->staticsound = (ent == 0) ? true : false;
    target_chan->use_loop = (flags & SND_STOP_LOOPING) ? false : true;
    target_chan->localsound = (flags & SND_LOCALSOUND) ? true : false;
    target_chan->dist_mult = (attn / SND_CLIP_DISTANCE);
    target_chan->master_vol = vol;
    target_chan->entnum = ent;
    target_chan->entchannel = chan;
    target_chan->basePitch = pitch;
    target_chan->isSentence = false;
    target_chan->sfx = sfx;
    // regular or streamed sound fx
    pSource = S_LoadSound(sfx);
    target_chan->name[0] = '\0';

    if(!pSource)
    {
        S_FreeChannel(target_chan);
        return;
    }

    SND_Spatialize(target_chan);

    // apply the sample offests
    target_chan->pMixer.sample = sample;
    target_chan->pMixer.forcedEndSample = end;

#ifdef XASH_DREAMCAST
    // Start sound on AICA
    if(pSource)
    {
        qboolean should_loop = (target_chan->use_loop && FBitSet(pSource->flags, SOUND_LOOPED));
        

        AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
        
        cmd->cmd = AICA_CMD_CHAN;
        cmd->timestamp = 0;
        cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
        cmd->cmd_id = target_chan->aica_channel;
        
        chan->cmd = AICA_CH_CMD_START;
        chan->base = pSource->aica_pos;
        chan->type = pSource->type;
        chan->length = pSource->size;
        
        chan->loop = should_loop;
        chan->loopstart = 0;
        chan->loopend = pSource->size;
        
        chan->freq = pSource->rate;
        chan->vol = (target_chan->leftvol + target_chan->rightvol) / 2;
        chan->pan = 128;  // Will be updated by spatialize
        
        snd_sh4_to_aica(tmp, cmd->size);
        
        aica_channels_in_use[target_chan->aica_channel] = true;
        target_chan->active = true;
        target_chan->start_time = Sys_DoubleTime();
    }
#endif

    // Init client entity mouth movement vars
    SND_InitMouth(ent, chan);
}

/*
=================
S_AmbientSound

Start playback of a sound, loaded into the static portion of the channel array.
Currently, this should be used for looping ambient sounds, looping sounds
that should not be interrupted until complete, non-creature sentences,
and one-shot ambient streaming sounds.  Can also play 'regular' sounds one-shot,
in case designers want to trigger regular game sounds.
Pitch changes playback pitch of wave by % above or below 100.  Ignored if pitch == 100

NOTE: volume is 0.0 - 1.0 and attenuation is 0.0 - 1.0 when passed in.
=================
*/
void S_AmbientSound(const vec3_t pos, int ent, sound_t handle, float fvol, float attn, int pitch, int flags)
{
    channel_t    *ch;
    wavdata_t    *pSource = NULL;
    sfx_t    *sfx = NULL;
    int    vol, fvox = 0;

    if(!dma.initialized) return;
    sfx = S_GetSfxByHandle(handle);
    if(!sfx) return;

    vol = bound(0, fvol * 255, 255);
    if(pitch <= 1) pitch = PITCH_NORM; // Invasion issues

    if(flags & (SND_STOP|SND_CHANGE_VOL|SND_CHANGE_PITCH))
    {
        if(S_AlterChannel(ent, CHAN_STATIC, sfx, vol, pitch, flags))
            return;
        if(flags & SND_STOP) return;
    }

    // pick a channel to play on from the static area
    ch = SND_PickStaticChannel(pos, sfx);
    if(!ch) return;

#ifdef XASH_DREAMCAST
    // Store AICA channel state
    int saved_aica_channel = ch->aica_channel;
    qboolean was_active = ch->active;
    
    // Stop previous sound if active
    if(was_active)
    {
        AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
        cmd->cmd = AICA_CMD_CHAN;
        cmd->timestamp = 0;
        cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
        cmd->cmd_id = ch->aica_channel;
        chan->cmd = AICA_CH_CMD_STOP;
        snd_sh4_to_aica(tmp, cmd->size);
    }
#endif

    VectorCopy(pos, ch->origin);
    ch->entnum = ent;


    CL_GetEntitySpatialization(ch);

    // Regular or stream sound
    pSource = S_LoadSound(sfx);
    ch->sfx = sfx;
    ch->isSentence = false;
    ch->name[0] = '\0';

    if(!pSource)
    {
        S_FreeChannel(ch);
        return;
    }

    pitch *= (sys_timescale.value + 1) / 2;

    // never update positions if source entity is 0
    ch->staticsound = (ent == 0) ? true : false;
    ch->use_loop = (flags & SND_STOP_LOOPING) ? false : true;
    ch->localsound = (flags & SND_LOCALSOUND) ? true : false;
    ch->master_vol = vol;
    ch->dist_mult = (attn / SND_CLIP_DISTANCE);
    ch->entchannel = CHAN_STATIC;
    ch->basePitch = pitch;

    SND_Spatialize(ch);

#ifdef XASH_DREAMCAST
    // Start sound on AICA
    if(pSource)
    {
        qboolean should_loop = (ch->use_loop && FBitSet(pSource->flags, SOUND_LOOPED));
        

        AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
        
        cmd->cmd = AICA_CMD_CHAN;
        cmd->timestamp = 0;
        cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
        cmd->cmd_id = ch->aica_channel;
        
        chan->cmd = AICA_CH_CMD_START;
        chan->base = pSource->aica_pos;
        chan->type = pSource->type;
        chan->length = pSource->size;
        
        chan->loop = should_loop;
        chan->loopstart = 0;
        chan->loopend = pSource->size;
        
        chan->freq = pSource->rate;
        chan->vol = (ch->leftvol + ch->rightvol) / 2;
        chan->pan = 128;  // Will be updated by spatialize
        
        snd_sh4_to_aica(tmp, cmd->size);
        
        aica_channels_in_use[ch->aica_channel] = true;
        ch->active = true;
        ch->start_time = Sys_DoubleTime();
    }
#endif
}

/*
==================
S_StartLocalSound
==================
*/
void S_StartLocalSound(  const char *name, float volume, qboolean reliable )
{
	sound_t	sfxHandle;
	int	flags = (SND_LOCALSOUND|SND_STOP_LOOPING);
	int	channel = CHAN_AUTO;

	if( reliable ) channel = CHAN_STATIC;

	if( !dma.initialized ) return;
	sfxHandle = S_RegisterSound( name );
	S_StartSound( NULL, s_listener.entnum, channel, sfxHandle, volume, ATTN_NONE, PITCH_NORM, flags );
}

/*
==================
S_GetCurrentStaticSounds

grab all static sounds playing at current channel
==================
*/
int S_GetCurrentStaticSounds( soundlist_t *pout, int size )
{
	int	sounds_left = size;
	int	i;

	if( !dma.initialized )
		return 0;

	for( i = MAX_DYNAMIC_CHANNELS; i < total_channels && sounds_left; i++ )
	{
		if( channels[i].entchannel == CHAN_STATIC && channels[i].sfx && channels[i].sfx->name[0] )
		{
			if( channels[i].isSentence && channels[i].name[0] )
				Q_strncpy( pout->name, channels[i].name, sizeof( pout->name ));
			else Q_strncpy( pout->name, channels[i].sfx->name, sizeof( pout->name ));
			pout->entnum = channels[i].entnum;
			VectorCopy( channels[i].origin, pout->origin );
			pout->volume = (float)channels[i].master_vol / 255.0f;
			pout->attenuation = channels[i].dist_mult * SND_CLIP_DISTANCE;
			pout->looping = ( channels[i].use_loop && FBitSet( channels[i].sfx->cache->flags, SOUND_LOOPED ));
			pout->pitch = channels[i].basePitch;
			pout->channel = channels[i].entchannel;
			pout->wordIndex = channels[i].wordIndex;
			pout->samplePos = channels[i].pMixer.sample;
			pout->forcedEnd = channels[i].pMixer.forcedEndSample;

			sounds_left--;
			pout++;
		}
	}

	return ( size - sounds_left );
}

/*
==================
S_GetCurrentStaticSounds

grab all static sounds playing at current channel
==================
*/
int S_GetCurrentDynamicSounds( soundlist_t *pout, int size )
{
	int	sounds_left = size;
	int	i, looped;

	if( !dma.initialized )
		return 0;

	for( i = 0; i < MAX_CHANNELS && sounds_left; i++ )
	{
		if( !channels[i].sfx || !channels[i].sfx->name[0] || !Q_stricmp( channels[i].sfx->name, "*default" ))
			continue;	// don't serialize default sounds

		looped = ( channels[i].use_loop && FBitSet( channels[i].sfx->cache->flags, SOUND_LOOPED ));

		if( channels[i].entchannel == CHAN_STATIC && looped && !Host_IsQuakeCompatible())
			continue;	// never serialize static looped sounds. It will be restoring in game code

		if( channels[i].isSentence && channels[i].name[0] )
			Q_strncpy( pout->name, channels[i].name, sizeof( pout->name ));
		else Q_strncpy( pout->name, channels[i].sfx->name, sizeof( pout->name ));
		pout->entnum = (channels[i].entnum < 0) ? 0 : channels[i].entnum;
		VectorCopy( channels[i].origin, pout->origin );
		pout->volume = (float)channels[i].master_vol / 255.0f;
		pout->attenuation = channels[i].dist_mult * SND_CLIP_DISTANCE;
		pout->pitch = channels[i].basePitch;
		pout->channel = channels[i].entchannel;
		pout->wordIndex = channels[i].wordIndex;
		pout->samplePos = channels[i].pMixer.sample;
		pout->forcedEnd = channels[i].pMixer.forcedEndSample;
		pout->looping = looped;

		sounds_left--;
		pout++;
	}

	return ( size - sounds_left );
}

/*
===================
S_InitAmbientChannels
===================
*/
static void S_InitAmbientChannels( void )
{
	int	ambient_channel;
	channel_t	*chan;

	for( ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++ )
	{
		chan = &channels[ambient_channel];

		chan->staticsound = true;
		chan->use_loop = true;
		chan->entchannel = CHAN_STATIC;
		chan->dist_mult = (ATTN_NONE / SND_CLIP_DISTANCE);
		chan->basePitch = PITCH_NORM;
	}
}

/*
===================
S_UpdateAmbientSounds
===================
*/
static void S_UpdateAmbientSounds( void )
{
	mleaf_t	*leaf;
	float	vol;
	int	ambient_channel;
	channel_t	*chan;

	if( !snd_ambient ) return;

	// calc ambient sound levels
	if( !cl.worldmodel ) return;

	leaf = Mod_PointInLeaf( s_listener.origin, cl.worldmodel->nodes, cl.worldmodel );

	if( !leaf || !s_ambient_level.value )
	{
		for( ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++ )
			channels[ambient_channel].sfx = NULL;
		return;
	}

	for( ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++ )
	{
		chan = &channels[ambient_channel];
		chan->sfx = S_GetSfxByHandle( ambient_sfx[ambient_channel] );

		// ambient is unused
		if( !chan->sfx )
		{
			chan->rightvol = 0;
			chan->leftvol = 0;
			continue;
		}

		vol = s_ambient_level.value * leaf->ambient_sound_level[ambient_channel];
		if( vol < 0 ) vol = 0;

		// don't adjust volume too fast
		if( chan->master_vol < vol )
		{
			chan->master_vol += s_listener.frametime * s_ambient_fade.value;
			if( chan->master_vol > vol ) chan->master_vol = vol;
		}
		else if( chan->master_vol > vol )
		{
			chan->master_vol -= s_listener.frametime * s_ambient_fade.value;
			if( chan->master_vol < vol ) chan->master_vol = vol;
		}

		chan->leftvol = chan->rightvol = chan->master_vol;
	}
}

/*
=============================================================================

		SOUND STREAM RAW SAMPLES

=============================================================================
*/
/*
===================
S_FindRawChannel
===================
*/
rawchan_t *S_FindRawChannel( int entnum, qboolean create )
{
	int	i, free;
	int	best, best_time;
	size_t	raw_samples = 0;
	rawchan_t	*ch;

	if( !entnum ) return NULL; // world is unused

	// check for replacement sound, or find the best one to replace
	best_time = 0x7fffffff;
	best = free = -1;

	for( i = 0; i < MAX_RAW_CHANNELS; i++ )
	{
		ch = raw_channels[i];

		if( free < 0 && !ch )
		{
			free = i;
		}
		else if( ch )
		{
			int	time;

			// exact match
			if( ch->entnum == entnum )
				return ch;

			time = ch->s_rawend - paintedtime;
			if( time < best_time )
			{
				best = i;
				best_time = time;
			}
		}
	}

	if( !create ) return NULL;

	if( free >= 0 ) best = free;
	if( best < 0 ) return NULL; // no free slots

	if( !raw_channels[best] )
	{
		raw_samples = MAX_RAW_SAMPLES;
		raw_channels[best] = Mem_Calloc( sndpool, sizeof( *ch ) + sizeof( portable_samplepair_t ) * raw_samples );
	}

	ch = raw_channels[best];
	ch->max_samples = raw_samples;
	ch->entnum = entnum;
	ch->s_rawend = 0;

	return ch;
}

/*
===================
S_RawSamplesStereo
===================
*/
static uint S_RawSamplesStereo( portable_samplepair_t *rawsamples, uint rawend, uint max_samples, uint samples, uint rate, word width, word channels, const byte *data )
{
	uint	fracstep, samplefrac;
	uint	src, dst;

	if( rawend < paintedtime )
		rawend = paintedtime;

	fracstep = ((double) rate / (double)SOUND_DMA_SPEED) * (double)(1 << S_RAW_SAMPLES_PRECISION_BITS);
	samplefrac = 0;

	if( width == 2 )
	{
		const short *in = (const short *)data;

		if( channels == 2 )
		{
			for( src = 0; src < samples; samplefrac += fracstep, src = ( samplefrac >> S_RAW_SAMPLES_PRECISION_BITS ))
			{
				dst = rawend++ & ( max_samples - 1 );
				rawsamples[dst].left = in[src*2+0];
				rawsamples[dst].right = in[src*2+1];
			}
		}
		else
		{
			for( src = 0; src < samples; samplefrac += fracstep, src = ( samplefrac >> S_RAW_SAMPLES_PRECISION_BITS ))
			{
				dst = rawend++ & ( max_samples - 1 );
				rawsamples[dst].left = in[src];
				rawsamples[dst].right = in[src];
			}
		}
	}
	else
	{
		if( channels == 2 )
		{
			const char *in = (const char *)data;

			for( src = 0; src < samples; samplefrac += fracstep, src = ( samplefrac >> S_RAW_SAMPLES_PRECISION_BITS ))
			{
				dst = rawend++ & ( max_samples - 1 );
				rawsamples[dst].left = in[src*2+0] << 8;
				rawsamples[dst].right = in[src*2+1] << 8;
			}
		}
		else
		{
			for( src = 0; src < samples; samplefrac += fracstep, src = ( samplefrac >> S_RAW_SAMPLES_PRECISION_BITS ))
			{
				dst = rawend++ & ( max_samples - 1 );
				rawsamples[dst].left = ( data[src] - 128 ) << 8;
				rawsamples[dst].right = ( data[src] - 128 ) << 8;
			}
		}
	}

	return rawend;
}

/*
===================
S_RawEntSamples
===================
*/
void S_RawEntSamples( int entnum, uint samples, uint rate, word width, word channels, const byte *data, int snd_vol )
{
	rawchan_t	*ch;

	if( snd_vol < 0 )
		snd_vol = 0;

	if( !( ch = S_FindRawChannel( entnum, true )))
		return;

	ch->master_vol = snd_vol;
	ch->dist_mult = (ATTN_NONE / SND_CLIP_DISTANCE);
	ch->s_rawend = S_RawSamplesStereo( ch->rawsamples, ch->s_rawend, ch->max_samples, samples, rate, width, channels, data );
	ch->leftvol = ch->rightvol = snd_vol;
}

/*
===================
S_RawSamples
===================
*/
void S_RawSamples( uint samples, uint rate, word width, word channels, const byte *data, int entnum )
{
	int	snd_vol = 128;

	if( entnum < 0 ) snd_vol = 256; // bg track or movie track

	S_RawEntSamples( entnum, samples, rate, width, channels, data, snd_vol );
}

/*
===================
S_PositionedRawSamples
===================
*/
void S_StreamAviSamples( void *Avi, int entnum, float fvol, float attn, float synctime )
{
	int	bufferSamples;
	int	fileSamples;
	byte	raw[MAX_RAW_SAMPLES];
	float	duration = 0.0f;
	int	r, fileBytes;
	rawchan_t	*ch = NULL;

	if( !dma.initialized || s_listener.paused || !CL_IsInGame( ))
		return;

#if XASH_DREAMCAST
	if( entnum < 0 || entnum >= DC_MAX_EDICTS )
#else
	if( entnum < 0 || entnum >= GI->max_edicts )
		return;
#endif

	if( !( ch = S_FindRawChannel( entnum, true )))
		return;

	if( ch->sound_info.rate == 0 )
	{
		if( !AVI_GetAudioInfo( Avi, &ch->sound_info ))
			return; // no audiotrack
	}

	ch->master_vol = bound( 0, fvol * 255, 255 );
	ch->dist_mult = (attn / SND_CLIP_DISTANCE);

	// see how many samples should be copied into the raw buffer
	if( ch->s_rawend < soundtime )
		ch->s_rawend = soundtime;

	// position is changed, synchronization is lost etc
	if( fabs( ch->oldtime - synctime ) > s_mixahead.value )
		ch->sound_info.loopStart = AVI_TimeToSoundPosition( Avi, synctime * 1000 );
	ch->oldtime = synctime; // keep actual time

	while( ch->s_rawend < soundtime + ch->max_samples )
	{
		wavdata_t	*info = &ch->sound_info;

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
		r = AVI_GetAudioChunk( Avi, raw, info->loopStart, fileBytes );
		info->loopStart += r; // advance play position

		if( r < fileBytes )
		{
			fileBytes = r;
			fileSamples = r / ( info->width * info->channels );
		}

		if( r > 0 )
		{
			// add to raw buffer
			ch->s_rawend = S_RawSamplesStereo( ch->rawsamples, ch->s_rawend, ch->max_samples,
			fileSamples, info->rate, info->width, info->channels, raw );
		}
		else break; // no more samples for this frame
	}
}

/*
===================
S_FreeIdleRawChannels

Free raw channel that have been idling for too long.
===================
*/
static void S_FreeIdleRawChannels( void )
{
	int	i;

	for( i = 0; i < MAX_RAW_CHANNELS; i++ )
	{
		rawchan_t	*ch = raw_channels[i];

		if( !ch ) continue;

		if( ch->s_rawend >= paintedtime )
			continue;
		
		if ( ch->entnum > 0 )
			SND_ForceCloseMouth( ch->entnum );

		if(( paintedtime - ch->s_rawend ) / SOUND_DMA_SPEED >= S_RAW_SOUND_IDLE_SEC )
		{
			raw_channels[i] = NULL;
			Mem_Free( ch );
		}
	}
}

/*
===================
S_ClearRawChannels
===================
*/
static void S_ClearRawChannels( void )
{
	int	i;

	for( i = 0; i < MAX_RAW_CHANNELS; i++ )
	{
		rawchan_t	*ch = raw_channels[i];

		if( !ch ) continue;
		ch->s_rawend = 0;
		ch->oldtime = -1;
	}
}

/*
===================
S_SpatializeRawChannels
===================
*/
static void S_SpatializeRawChannels( void )
{
	int	i;

	for( i = 0; i < MAX_RAW_CHANNELS; i++ )
	{
		rawchan_t	*ch = raw_channels[i];
		vec3_t	source_vec;
		float	dist, dot;

		if( !ch ) continue;

		if( ch->s_rawend < paintedtime )
		{
			ch->leftvol = ch->rightvol = 0;
			continue;
		}
#if XASH_DREAMCAST
		// spatialization
		if( !S_IsClient( ch->entnum ) && ch->dist_mult && ch->entnum >= 0 && ch->entnum < DC_MAX_EDICTS )
#else
		// spatialization
		if( !S_IsClient( ch->entnum ) && ch->dist_mult && ch->entnum >= 0 && ch->entnum < GI->max_edicts )
#endif
		{
			if( !CL_GetMovieSpatialization( ch ))
			{
				// origin is null and entity not exist on client
				ch->leftvol = ch->rightvol = 0;
			}
			else
			{
				VectorSubtract( ch->origin, s_listener.origin, source_vec );

				// normalize source_vec and get distance from listener to source
				dist = VectorNormalizeLength( source_vec );
				dot = DotProduct( s_listener.right, source_vec );

				// don't pan sounds with no attenuation
				if( ch->dist_mult <= 0.0f ) dot = 0.0f;

				int pan = 128;


				// fill out channel volumes for single location
				S_SpatializeChannel( &ch->leftvol, &ch->rightvol, ch->master_vol, 1.0f, dot, dist * ch->dist_mult, &pan );
			}
		}
		else
		{
			ch->leftvol = ch->rightvol = ch->master_vol;
		}
	}
}

/*
===================
S_FreeRawChannels
===================
*/
static void S_FreeRawChannels( void )
{
	int	i;

	// free raw samples
	for( i = 0; i < MAX_RAW_CHANNELS; i++ )
	{
		if( raw_channels[i] )
			Mem_Free( raw_channels[i] );
	}

	memset( raw_channels, 0, sizeof( raw_channels ));
}

//=============================================================================

/*
==================
S_ClearBuffer
==================
*/
static void S_ClearBuffer( void )
{
	S_ClearRawChannels();

	SNDDMA_BeginPainting ();
	if( dma.buffer ) memset( dma.buffer, 0, dma.samples * 2 );
	SNDDMA_Submit ();
#ifndef XASH_DREAMCAST

	MIX_ClearAllPaintBuffers( PAINTBUFFER_SIZE, true );
#endif
}

/*
==================
S_StopSound

stop all sounds for entity on a channel.
==================
*/
void GAME_EXPORT S_StopSound( int entnum, int channel, const char *soundname )
{
	sfx_t	*sfx;

	if( !dma.initialized ) return;
	sfx = S_FindName( soundname, NULL );
	S_AlterChannel( entnum, channel, sfx, 0, 0, SND_STOP );
}

/*
==================
S_StopAllSounds
==================
*/
void S_StopAllSounds( qboolean ambient )
{
	int	i;

	if( !dma.initialized ) return;
	total_channels = MAX_DYNAMIC_CHANNELS;	// no statics

	for( i = 0; i < MAX_CHANNELS; i++ )
	{
		if( !channels[i].sfx ) continue;
		S_FreeChannel( &channels[i] );
	}

	DSP_ClearState();

	// clear all the channels
	memset( channels, 0, sizeof( channels ));

	// restart the ambient sounds
	if( ambient ) S_InitAmbientChannels ();

	S_ClearBuffer ();

	// clear any remaining soundfade
	memset( &soundfade, 0, sizeof( soundfade ));
}

/*
==============
S_GetSoundtime

update global soundtime

(was part of platform code)
===============
*/
static int S_GetSoundtime( void )
{
	static int buffers, oldsamplepos;
	int samplepos, fullsamples;

	fullsamples = dma.samples / 2;

	// it is possible to miscount buffers
	// if it has wrapped twice between
	// calls to S_Update.  Oh well.
	samplepos = dma.samplepos;

	if( samplepos < oldsamplepos )
	{
		buffers++; // buffer wrapped

		if( paintedtime > 0x40000000 )
		{
			// time to chop things off to avoid 32 bit limits
			buffers     = 0;
			paintedtime = fullsamples;
			S_StopAllSounds( true );
		}
	}

	oldsamplepos = samplepos;

	return ( buffers * fullsamples + samplepos / 2 );
}

//=============================================================================
static void S_UpdateChannels( void )
{
	uint	endtime;
	int	samps;

	SNDDMA_BeginPainting();

	if( !dma.buffer ) return;

	// updates DMA time
	soundtime = S_GetSoundtime();

	// soundtime - total samples that have been played out to hardware at dmaspeed
	// paintedtime - total samples that have been mixed at speed
	// endtime - target for samples in mixahead buffer at speed
	endtime = soundtime + s_mixahead.value * SOUND_DMA_SPEED;
	samps = dma.samples >> 1;

	if((int)(endtime - soundtime) > samps )
		endtime = soundtime + samps;

	if(( endtime - paintedtime ) & 0x3 )
	{
		// the difference between endtime and painted time should align on
		// boundaries of 4 samples. this is important when upsampling from 11khz -> 44khz.
		endtime -= ( endtime - paintedtime ) & 0x3;
	}
#ifndef XASH_DREAMCAST
	MIX_PaintChannels( endtime );
#endif

	SNDDMA_Submit();
}

/*
=================
S_ExtraUpdate

Don't let sound skip if going slow
=================
*/
void S_ExtraUpdate( void )
{
	if( !dma.initialized ) return;
	S_UpdateChannels ();
}

/*
============
S_UpdateFrame

update listener position
============
*/
void S_UpdateFrame( struct ref_viewpass_s *rvp )
{
	if( !FBitSet( rvp->flags, RF_DRAW_WORLD ) || FBitSet( rvp->flags, RF_ONLY_CLIENTDRAW ))
		return;

	VectorCopy( rvp->vieworigin, s_listener.origin );
	AngleVectors( rvp->viewangles, s_listener.forward, s_listener.right, s_listener.up );
	s_listener.entnum = rvp->viewentity; // can be camera entity too
}

/*
============
SND_UpdateSound

Called once each time through the main loop
============
*/
void SND_UpdateSound( void )
{
	int		i, j, total;
	channel_t		*ch, *combine;
	con_nprint_t	info;

	if( !dma.initialized ) return;

	// if the loading plaque is up, clear everything
	// out to make sure we aren't looping a dirty
	// dma buffer while loading
	// update any client side sound fade
	S_UpdateSoundFade();

	// release raw-channels that no longer used more than 10 secs
	S_FreeIdleRawChannels();

	s_listener.frametime = (cl.time - cl.oldtime);
	s_listener.waterlevel = cl.local.waterlevel;
	s_listener.active = CL_IsInGame();
	s_listener.inmenu = cls.key_dest == key_menu;
	s_listener.paused = cl.paused;

	// update general area ambient sound sources
	S_UpdateAmbientSounds();

	combine = NULL;

	// update spatialization for static and dynamic sounds
	for( i = NUM_AMBIENTS, ch = channels + NUM_AMBIENTS; i < total_channels; i++, ch++ )
	{
		if( !ch->sfx ) continue;
		SND_Spatialize( ch ); // respatialize channel

		if( !ch->leftvol && !ch->rightvol )
			continue;

		// try to combine static sounds with a previous channel of the same
		// sound effect so we don't mix five torches every frame
		// g-cont: perfomance option, probably kill stereo effect in most cases
		if( i >= MAX_DYNAMIC_CHANNELS && s_combine_sounds.value )
		{
			// see if it can just use the last one
			if( combine && combine->sfx == ch->sfx )
			{
				combine->leftvol += ch->leftvol;
				combine->rightvol += ch->rightvol;
				ch->leftvol = ch->rightvol = 0;
				continue;
			}

			// search for one
			combine = channels + MAX_DYNAMIC_CHANNELS;

			for( j = MAX_DYNAMIC_CHANNELS; j < i; j++, combine++ )
			{
				if( combine->sfx == ch->sfx )
					break;
			}

			if( j == total_channels )
			{
				combine = NULL;
			}
			else
			{
				if( combine != ch )
				{
					combine->leftvol += ch->leftvol;
					combine->rightvol += ch->rightvol;
					ch->leftvol = ch->rightvol = 0;
				}
				continue;
			}
		}
	}

	S_SpatializeRawChannels();

	// debugging output
	if( s_show.value != 0.0f )
	{
		info.color[0] = 1.0f;
		info.color[1] = 0.6f;
		info.color[2] = 0.0f;
		info.time_to_live = 0.5f;

		for( i = 0, total = 1, ch = channels; i < MAX_CHANNELS; i++, ch++ )
		{
			if( ch->sfx && ( ch->leftvol || ch->rightvol ))
			{
				info.index = total;
				Con_NXPrintf( &info, "chan %i, pos (%.f %.f %.f) ent %i, lv%3i rv%3i %s\n",
				i, ch->origin[0], ch->origin[1], ch->origin[2], ch->entnum, ch->leftvol, ch->rightvol, ch->sfx->name );
				total++;
			}
		}

		VectorSet( info.color, 1.0f, 1.0f, 1.0f );
		info.index = 0;

		Con_NXPrintf( &info, "room_type: %i (%s) ----(%i)---- painted: %i\n", idsp_room, Cvar_VariableString( "dsp_coeff_table" ), total - 1, paintedtime );
	}

	S_StreamBackgroundTrack ();
	S_StreamSoundTrack ();
#ifndef XASH_DREAMCAST
	// mix some sound
	S_UpdateChannels ();
#endif
}

/*
===============================================================================

console functions

===============================================================================
*/
static void S_Play_f( void )
{
	if( Cmd_Argc() == 1 )
	{
		Con_Printf( S_USAGE "play <soundfile>\n" );
		return;
	}

	S_StartLocalSound( Cmd_Argv( 1 ), VOL_NORM, false );
}

static void S_Play2_f( void )
{
	int	i = 1;

	if( Cmd_Argc() == 1 )
	{
		Con_Printf( S_USAGE "play2 <soundfile>\n" );
		return;
	}

	while( i < Cmd_Argc( ))
	{
		S_StartLocalSound( Cmd_Argv( i ), VOL_NORM, true );
		i++;
	}
}

static void S_PlayVol_f( void )
{
	if( Cmd_Argc() == 1 )
	{
		Con_Printf( S_USAGE "playvol <soundfile volume>\n" );
		return;
	}

	S_StartLocalSound( Cmd_Argv( 1 ), Q_atof( Cmd_Argv( 2 )), false );
}

static void S_Say( const char *name, qboolean reliable )
{
	char sentence[1024];

	// predefined vox sentence
	if( name[0] == '!' )
	{
		S_StartLocalSound( name, 1.0f, reliable );
		return;
	}

	Q_snprintf( sentence, sizeof( sentence ), "!#%s", name );
	S_StartLocalSound( sentence, 1.0f, reliable );
}

static void S_Say_f( void )
{
	if( Cmd_Argc() == 1 )
	{
		Con_Printf( S_USAGE "speak <vox sentence>\n" );
		return;
	}

	S_Say( Cmd_Argv( 1 ), false );
}

static void S_SayReliable_f( void )
{
	if( Cmd_Argc() == 1 )
	{
		Con_Printf( S_USAGE "spk <vox sentence>\n" );
		return;
	}

	S_Say( Cmd_Argv( 1 ), true );
}

/*
=================
S_Music_f
=================
*/
#if XASH_DREAMCAST
static void S_Music_f(void)
{
    int c = Cmd_Argc();

    if(c == 1)
    {
        S_StopBackgroundTrack();
    }
    else if(c == 2)
    {
        string intro, main, track;
        char fullpath[MAX_VA_STRING];
        const char *ext = "wav";  // Dreamcast uses WAV

        Q_strncpy(track, Cmd_Argv(1), sizeof(track));
        Q_snprintf(intro, sizeof(intro), "%s_intro", Cmd_Argv(1));
        Q_snprintf(main, sizeof(main), "%s_main", Cmd_Argv(1));

        // Try sound/music paths with full path specification
        Q_snprintf(fullpath, sizeof(fullpath), "sound/music/%s.%s", track, ext);
        Con_DPrintf("S_Music_f: Trying track: %s\n", fullpath);

        if(FS_FileExists(fullpath, false))
        {
            S_StartBackgroundTrack(fullpath, NULL, 0, true);  // true = use full path
            return;
        }

        // Try intro/main combination
        char intro_path[MAX_VA_STRING];
        char main_path[MAX_VA_STRING];
        
        Q_snprintf(intro_path, sizeof(intro_path), "sound/music/%s.%s", intro, ext);
        Q_snprintf(main_path, sizeof(main_path), "sound/music/%s.%s", main, ext);

        if(FS_FileExists(intro_path, false) && FS_FileExists(main_path, false))
        {
            S_StartBackgroundTrack(intro_path, main_path, 0, true);
            return;
        }
    }
    else if(c == 3)
    {
        S_StartBackgroundTrack(Cmd_Argv(1), Cmd_Argv(2), 0, false);
    }
    else if(c == 4 && Q_atoi(Cmd_Argv(3)) != 0)
    {
        S_StartBackgroundTrack(Cmd_Argv(1), Cmd_Argv(2), Q_atoi(Cmd_Argv(3)), false);
    }
    else Con_Printf(S_USAGE "music <musicfile> [loopfile]\n");
}
#else
static void S_Music_f( void )
{
	int	c = Cmd_Argc();

	// run background track
	if( c == 1 )
	{
		// blank name stopped last track
		S_StopBackgroundTrack();
	}
	else if( c == 2 )
	{
		string	intro, main, track;
		const char	*ext[] = { "mp3", "wav" };
		int	i;

		Q_strncpy( track, Cmd_Argv( 1 ), sizeof( track ));
		Q_snprintf( intro, sizeof( intro ), "%s_intro", Cmd_Argv( 1 ));
		Q_snprintf( main, sizeof( main ), "%s_main", Cmd_Argv( 1 ));

		for( i = 0; i < 2; i++ )
		{
			char intro_path[MAX_VA_STRING];
			char main_path[MAX_VA_STRING];
			char track_path[MAX_VA_STRING];

			Q_snprintf( intro_path, sizeof( intro_path ), "media/%s.%s", intro, ext[i] );
			Q_snprintf( main_path, sizeof( main_path ), "media/%s.%s", main, ext[i] );

			if( FS_FileExists( intro_path, false ) && FS_FileExists( main_path, false ))
			{
				// combined track with introduction and main loop theme
				S_StartBackgroundTrack( intro, main, 0, false );
				break;
			}

			Q_snprintf( track_path, sizeof( track_path ), "media/%s.%s", track, ext[i] );

			if( FS_FileExists( track_path, false ))
			{
				// single non-looped theme
				S_StartBackgroundTrack( track, NULL, 0, false );
				break;
			}
		}

	}
	else if( c == 3 )
	{
		S_StartBackgroundTrack( Cmd_Argv( 1 ), Cmd_Argv( 2 ), 0, false );
	}
	else if( c == 4 && Q_atoi( Cmd_Argv( 3 )) != 0 )
	{
		// restore command for singleplayer: all arguments are valid
		S_StartBackgroundTrack( Cmd_Argv( 1 ), Cmd_Argv( 2 ), Q_atoi( Cmd_Argv( 3 )), false );
	}
	else Con_Printf( S_USAGE "music <musicfile> [loopfile]\n" );
}
#endif
/*
=================
S_StopSound_f
=================
*/
static void S_StopSound_f( void )
{
	S_StopAllSounds( true );
}

/*
=================
S_SoundFade_f
=================
*/
static void S_SoundFade_f( void )
{
	int	c = Cmd_Argc();
	float	fadeTime = 5.0f;

	if( c == 2 )
		fadeTime = bound( 1.0f, atof( Cmd_Argv( 1 )), 60.0f );

	S_FadeClientVolume( 100.0f, fadeTime, 1.0f, 0.0f );
	snd_fade_sequence = true;
}

/*
=================
S_SoundInfo_f
=================
*/
void S_SoundInfo_f( void )
{
	Con_Printf( "Audio backend: %s\n", dma.backendName );
	Con_Printf( "%5d channel(s)\n", 2 );
	Con_Printf( "%5d samples\n", dma.samples );
	Con_Printf( "%5d bits/sample\n", 16 );
	Con_Printf( "%5d bytes/sec\n", SOUND_DMA_SPEED );
	Con_Printf( "%5d total_channels\n", total_channels );

	S_PrintBackgroundTrackState ();
}

/*
=================
S_VoiceRecordStart_f
=================
*/
static void S_VoiceRecordStart_f( void )
{
	if( cls.state != ca_active )
		return;
	
#if !XASH_DREAMCAST
	Voice_RecordStart();
#endif
}

/*
=================
S_VoiceRecordStop_f
=================
*/
static void S_VoiceRecordStop_f( void )
{
#if !XASH_DREAMCAST
	if( cls.state != ca_active || !Voice_IsRecording() )
		return;

	CL_AddVoiceToDatagram();
	Voice_RecordStop();
#endif
}

/*
================
S_Init
================
*/
qboolean S_Init( void )
{

	Cvar_RegisterVariable( &s_volume );
	Cvar_RegisterVariable( &s_musicvolume );
	Cvar_RegisterVariable( &s_mixahead );
	Cvar_RegisterVariable( &s_show );
	Cvar_RegisterVariable( &s_lerping );
	Cvar_RegisterVariable( &s_ambient_level );
	Cvar_RegisterVariable( &s_ambient_fade );
	Cvar_RegisterVariable( &s_combine_sounds );
	Cvar_RegisterVariable( &snd_mute_losefocus );
	Cvar_RegisterVariable( &s_test );
	Cvar_RegisterVariable( &s_samplecount );
	Cvar_RegisterVariable( &s_warn_late_precache );



	Cmd_AddCommand( "play", S_Play_f, "playing a specified sound file" );
	Cmd_AddCommand( "play2", S_Play2_f, "playing a group of specified sound files" ); // nehahra stuff
	Cmd_AddCommand( "playvol", S_PlayVol_f, "playing a specified sound file with specified volume" );
	Cmd_AddCommand( "stopsound", S_StopSound_f, "stop all sounds" );
	// HLU SDK have command with the same name
	Cmd_AddCommandWithFlags( "music", S_Music_f, "starting a background track", CMD_OVERRIDABLE );
	Cmd_AddCommand( "soundlist", S_SoundList_f, "display loaded sounds" );
	Cmd_AddCommand( "s_info", S_SoundInfo_f, "print sound system information" );
	Cmd_AddCommand( "s_fade", S_SoundFade_f, "fade all sounds then stop all" );
	Cmd_AddCommand( "+voicerecord", S_VoiceRecordStart_f, "start voice recording" );
	Cmd_AddCommand( "-voicerecord", S_VoiceRecordStop_f, "stop voice recording" );
	Cmd_AddCommand( "spk", S_SayReliable_f, "reliable play a specified sententce" );
	Cmd_AddCommand( "speak", S_Say_f, "playing a specified sententce" );

	dma.backendName = "None";
	if( !SNDDMA_Init( ) )
	{
		Con_Printf( "Audio: sound system can't be initialized\n" );
		return false;
	}

	sndpool = Mem_AllocPool( "Sound Zone" );
	soundtime = 0;
	paintedtime = 0;

	// clear ambient sounds
	memset( ambient_sfx, 0, sizeof( ambient_sfx ));

#ifndef XASH_DREAMCAST
	MIX_InitAllPaintbuffers ();
#endif
	SX_Init ();
#ifndef XASH_DREAMCAST
	S_InitScaletable ();
#endif
	S_StopAllSounds ( true );
	S_InitSounds ();
#ifndef XASH_DREAMCAST
	VOX_Init ();
#endif
	return true;
}

// =======================================================================
// Shutdown sound engine
// =======================================================================
void S_Shutdown( void )
{
	if( !dma.initialized ) return;

	Cmd_RemoveCommand( "play" );
	Cmd_RemoveCommand( "playvol" );
	Cmd_RemoveCommand( "stopsound" );
	if( Cmd_Exists( "music" ))
		Cmd_RemoveCommand( "music" );
	Cmd_RemoveCommand( "soundlist" );
	Cmd_RemoveCommand( "s_info" );
	Cmd_RemoveCommand( "s_fade" );
	Cmd_RemoveCommand( "+voicerecord" );
	Cmd_RemoveCommand( "-voicerecord" );
	Cmd_RemoveCommand( "speak" );
	Cmd_RemoveCommand( "spk" );

	S_StopAllSounds (false);
	S_FreeRawChannels ();
	S_FreeSounds ();
#ifndef XASH_DREAMCAST
	VOX_Shutdown ();
#endif
	SX_Free ();

	SNDDMA_Shutdown ();
#ifndef XASH_DREAMCAST
	MIX_FreeAllPaintbuffers ();
#endif
	Mem_FreePool( &sndpool );
}
