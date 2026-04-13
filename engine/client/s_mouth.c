/*
s_mouth.c - animate mouth
Copyright (C) 2010 Uncle Mike

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
#include "const.h"
#ifdef XASH_DREAMCAST
#include "platform/dreamcast/AudioEngine.h"
#endif

#define CAVGSAMPLES		10

void SND_InitMouth( int entnum, int entchannel )
{
	if(( entchannel == CHAN_VOICE || entchannel == CHAN_STREAM ) && entnum > 0 )
	{
		SND_ForceInitMouth( entnum );
	}
}

void SND_CloseMouth( channel_t *ch )
{
	if( ch->entchannel == CHAN_VOICE || ch->entchannel == CHAN_STREAM )
	{
		SND_ForceCloseMouth( ch->entnum );
	}
}

void SND_MoveMouth8( channel_t *ch, wavdata_t *pSource, int count )
{
	cl_entity_t	*clientEntity;
	signed char		*pdata = NULL;
	mouth_t		*pMouth = NULL;
	int		scount, pos = 0;
	int		savg, data;
	uint 		i;

	clientEntity = CL_GetEntityByIndex( ch->entnum );
	if( !clientEntity ) return;

	pMouth = &clientEntity->mouth;

	if( ch->isSentence )
	{
		if( ch->currentWord )
			pos = ch->currentWord->sample;
	}
	else pos = ch->pMixer.sample;

	count = S_GetOutputData( pSource, (void**)&pdata, pos, count, ch->use_loop );
	if( pdata == NULL ) return;

	i = 0;
	scount = pMouth->sndcount;
	savg = 0;

	while( i < count && scount < CAVGSAMPLES )
	{
		data = pdata[i];
		savg += abs( data );

		i += 80 + ((byte)data & 0x1F);
		scount++;
	}

	pMouth->sndavg += savg;
	pMouth->sndcount = (byte)scount;

	if( pMouth->sndcount >= CAVGSAMPLES )
	{
		pMouth->mouthopen = pMouth->sndavg / CAVGSAMPLES;
		pMouth->sndavg = 0;
		pMouth->sndcount = 0;
	}
}

void SND_MoveMouth16( channel_t *ch, wavdata_t *pSource, int count )
{
	cl_entity_t	*clientEntity;
	short		*pdata = NULL;
	mouth_t		*pMouth = NULL;
	int		savg, data;
	int		scount, pos = 0;
	uint 		i;

	clientEntity = CL_GetEntityByIndex( ch->entnum );
	if( !clientEntity ) return;

	pMouth = &clientEntity->mouth;

	if( ch->isSentence )
	{
		if( ch->currentWord )
			pos = ch->currentWord->sample;
	}
	else pos = ch->pMixer.sample;

	count = S_GetOutputData( pSource, (void**)&pdata, pos, count, ch->use_loop );
	if( pdata == NULL ) return;

	i = 0;
	scount = pMouth->sndcount;
	savg = 0;

	while( i < count && scount < CAVGSAMPLES )
	{
		data = pdata[i];
		data = (bound( -32767, data, 0x7ffe ) >> 8);
		savg += abs( data );

		i += 80 + ((byte)data & 0x1F);
		scount++;
	}

	pMouth->sndavg += savg;
	pMouth->sndcount = (byte)scount;

	if( pMouth->sndcount >= CAVGSAMPLES )
	{
		pMouth->mouthopen = pMouth->sndavg / CAVGSAMPLES;
		pMouth->sndavg = 0;
		pMouth->sndcount = 0;
	}
}

void SND_ForceInitMouth( int entnum )
{
	cl_entity_t *clientEntity;

	clientEntity = CL_GetEntityByIndex( entnum );

	if( clientEntity )
	{
		clientEntity->mouth.mouthopen = 0;
		clientEntity->mouth.sndavg = 0;
		clientEntity->mouth.sndcount = 0;
	}
}

void SND_ForceCloseMouth( int entnum )
{
	cl_entity_t *clientEntity;

	clientEntity = CL_GetEntityByIndex( entnum );

	if( clientEntity )
		clientEntity->mouth.mouthopen = 0;
}

void SND_MoveMouthRaw( rawchan_t *ch, portable_samplepair_t *pData, int count )
{
	cl_entity_t	*clientEntity;
	mouth_t		*pMouth = NULL;
	int		savg, data;
	int		scount = 0;
	uint 		i;

	clientEntity = CL_GetEntityByIndex( ch->entnum );
	if( !clientEntity ) return;

	pMouth = &clientEntity->mouth;

	if( pData == NULL )
		return;

	i = 0;
	scount = pMouth->sndcount;
	savg = 0;

	while ( i < count && scount < CAVGSAMPLES )
	{
		data = pData[i].left; // mono sound anyway
		data = ( bound( -32767, data, 0x7ffe ) >> 8 );
		savg += abs( data );

		i += 80 + ( (byte)data & 0x1F );
		scount++;
	}

	pMouth->sndavg += savg;
	pMouth->sndcount = (byte)scount;

	if ( pMouth->sndcount >= CAVGSAMPLES )
	{
		pMouth->mouthopen = pMouth->sndavg / CAVGSAMPLES;
		pMouth->sndavg = 0;
		pMouth->sndcount = 0;
	}
}

#ifdef XASH_DREAMCAST
/*
=================
SND_UpdateMouthDC

Called from SND_UpdateSound every game frame for every active CHAN_VOICE /
CHAN_STREAM channel.
=================
*/
/* Our envelope stores avg(|sample|>>8): 0-127.
 * PC SND_MoveMouth16 stores avg(sample/256) over CAVGSAMPLES: ~0-20 due to oscillation cancellation.
 * Divide by 4 → typical speech gives mouthopen 6-12 (9-19% jaw open), loud peaks 25-30 (39-47%). */
#define DC_MOUTH_DIVISOR 4

void SND_UpdateMouthDC( channel_t *ch )
{
	cl_entity_t  *clientEntity;
	mouth_t      *pMouth;
	int           ae_idx;
	uint32_t      sample_pos;
	uint32_t      env_idx;
	uint8_t      *amp_table;
	uint16_t      amp_count;
	float         t, amp, osc;

	if( ch->entnum <= 0 ) return;
	if( ch->entchannel != CHAN_VOICE && ch->entchannel != CHAN_STREAM ) return;
	if( !ch->sfx ) return;
	if( !ch->active ) return;

	clientEntity = CL_GetEntityByIndex( ch->entnum );
	if( !clientEntity ) return;

	pMouth = &clientEntity->mouth;

	/* Get AudioEngine stream/SFX index from the sfx cache.
	   For sentences ch->sfx == ch->words[0].sfx (set by VOX_LoadSound),
	   so cache->aica_pos gives the AudioEngine index for the first word. */
	if( !ch->sfx->cache || ch->sfx->cache->aica_pos < 0 )
	{
		pMouth->mouthopen = 0;
		return;
	}
	ae_idx = (int)ch->sfx->cache->aica_pos;

	/* Query current AICA playback position */
	sample_pos = AudioEngine_GetSamplePosition( ae_idx );

	/* Choose amplitude table from the right AudioEngine struct */
	amp_table = NULL;
	amp_count = 0;
	if( ae_idx < AUDIO_ENGINE_MAX_STREAMS )
	{
		struct stream_info *si = AudioEngine_getStreamInfo( ae_idx );
		if( si && si->amplitude_count > 0 )
		{
			amp_table = si->amplitude;
			amp_count = si->amplitude_count;
		}
	}
	else
	{
		struct sfx_info *si = AudioEngine_getSfxInfo( ae_idx );
		if( si && si->amplitude_count > 0 )
		{
			amp_table = si->amplitude;
			amp_count = si->amplitude_count;
		}
	}

	if( amp_table && amp_count > 0 )
	{
		env_idx = sample_pos / AE_ENVELOPE_STEP;
		if( env_idx >= amp_count ) env_idx = amp_count - 1;
		pMouth->mouthopen = (byte)( amp_table[env_idx] / DC_MOUTH_DIVISOR );
		return;
	}

	/* Fallback: no envelope data (e.g. sound not yet played back via AE).
	   Use a 2.5 Hz oscillator scaled by channel volume. */
	t   = (float)( Sys_DoubleTime() - ch->start_time );
	amp = ( ch->leftvol + ch->rightvol ) * ( 0.5f / 255.0f );
	osc = fabsf( sinf( t * ( 2.0f * M_PI * 2.5f ) ) );
	pMouth->mouthopen = (byte)bound( 0, (int)( osc * amp * 200.0f ), 255 );
}
#endif /* XASH_DREAMCAST */

