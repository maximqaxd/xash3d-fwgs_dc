/*
s_dc.c - DC sound component
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

#include "kos.h"
#include "common.h"
#include "sound.h"
#include "platform/platform.h"
#include <dc/sound/sound.h>

#if XASH_SOUND == SOUND_KOS


qboolean SNDDMA_Init(void)
{

	snd_init();
	snd_stream_init_ex(4, music_buffer);
	dma.format.speed    = SOUND_DMA_SPEED;
	dma.format.channels = 2;
	dma.format.width    = 2;
	dma.samples         = 2048 * 2;
	dma.buffer          = Z_Calloc( dma.samples * 2 );
	dma.samplepos       = 0;
	dma.initialized = true;
	dma.backendName = "AICA SPU";
	return true;
}

void SNDDMA_Shutdown(void)
{
    snd_shutdown();
	snd_stream_shutdown(); 
}

void SNDDMA_Submit(void)
{
    // TODO: implement sound
// 	snd_stream_poll(stream);

}

void SNDDMA_BeginPainting(void)
{
    // TODO: implement sound
}

void S_Activate(qboolean active)
{
   // TODO: implement sound
}

/*
===========
VoiceCapture_Init
===========
*/
qboolean VoiceCapture_Init( void )
{
    // stub
	return false;
}

/*
===========
VoiceCapture_Activate
===========
*/
qboolean VoiceCapture_Activate( qboolean activate )
{
    // stub
	return false;
}

/*
===========
VoiceCapture_Lock
===========
*/
qboolean VoiceCapture_Lock( qboolean lock )
{
    // stub
	return false;
}

/*
==========
VoiceCapture_Shutdown
==========
*/
void VoiceCapture_Shutdown( void )
{
    // stub
}

#endif // XASH_SOUND == SOUND_KOS
