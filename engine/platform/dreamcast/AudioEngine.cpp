/**
 * Audio Engine For Dreamcast by Josh Pearson.
 * Leverages routines built for DCA project, thanks to SKMP for the core functionality here :-)
 * 
 * There are Three Library Functions In Total:
 * 
 * bool AudioEngine_Initialise(void)
 * uint8 AudioEngine_Load(const char * fname, uint32_t seek_bytes_aligned);
 * void AudioEngine_Play(uint8 nStream, uint8 volume, uint8 pan);
 * 
 * 
 * Sound Files Can be Any Length! 
 * If it is small enough to load into a single shot, it will be loaded directly into Sound RAM and played as a SFX.
 * If the file is too large to be played in a single shot, it will be streamed into SRAM as needed in a single thread.
 * 
 */
#include <kos.h>
#include <string>
#include <assert.h>
#include <thread>
#include <mutex>
#include <sys/types.h>  // for off_t

#include "AicaInterface.h"
#include "AudioEngine.h"

// Xash3D VFS support - forward declarations
#ifdef __cplusplus
extern "C" {
#endif
// Forward declare dc_file_t (defined in filesystem)
struct file_s;
typedef struct file_s dc_file_t;

// Forward declare VFS functions we need
typedef off_t fs_offset_t;
fs_offset_t FS_Read(dc_file_t *file, void *buffer, size_t buffersize);
int FS_Seek(dc_file_t *file, fs_offset_t offset, int whence);
fs_offset_t FS_Tell(dc_file_t *file);
int FS_Close(dc_file_t *file);
fs_offset_t FS_FileLength(dc_file_t *f);
#ifdef __cplusplus
}
#endif

stream_info streams[AUDIO_ENGINE_MAX_STREAMS];
sfx_chnnel sfx_channels[AUDIO_ENGINE_MAX_CHANNELS];
sfx_info sfx[AUDIO_ENGINE_MAX_SFX];
static uint8_t sfx_buffer[AICA_MAX_SAMPLES * 2];
static volatile uint32_t ticks;

std::mutex channel_mtx;
std::thread snd_thread;
 
static void StreamRead(int nStream, void* buf, uint32_t size) {
	if(streams[nStream].is_memory) {
		// Read from memory
		uint32_t available = streams[nStream].mem_size - streams[nStream].mem_offset;
		uint32_t to_read = (size < available) ? size : available;
		memcpy(buf, streams[nStream].mem_data + streams[nStream].mem_offset, to_read);
		streams[nStream].mem_offset += to_read;
		// Zero-pad if we read less than requested
		if(to_read < size) {
			memset((uint8_t*)buf + to_read, 0, size - to_read);
		}
	} else if(streams[nStream].is_vfs && streams[nStream].vfs_file) {
		// Read from Xash3D VFS file handle
		dc_file_t *vfs_file = (dc_file_t *)streams[nStream].vfs_file;
		FS_Read(vfs_file, buf, size);
	} else {
		// Read from KOS file handle
		fs_read(streams[nStream].fd, buf, size);
	}
}

void AudioEngine_LoadFirstChunk(int nStream) {
	 uint32_t read_size = streams[nStream].stereo ? STREAM_STAGING_READ_SIZE_STEREO : STREAM_STAGING_READ_SIZE_MONO;
	 StreamRead(nStream, streams[nStream].buffer, read_size);
	if (streams[nStream].stereo) {
		snd_adpcm_split((uint32_t*)streams[nStream].buffer, (uint32_t*)sfx_buffer, (uint32_t*)sfx_buffer + STREAM_STAGING_READ_SIZE_MONO, STREAM_STAGING_READ_SIZE_STEREO);
		spu_memload_sq(streams[nStream].aica_buffers[0], (uint32_t*)sfx_buffer, STREAM_STAGING_READ_SIZE_STEREO/2);
		spu_memload_sq(streams[nStream].aica_buffers[1], (uint32_t*)sfx_buffer + STREAM_STAGING_READ_SIZE_MONO, STREAM_STAGING_READ_SIZE_STEREO/2);
	}		
	else {
		spu_memload_sq(streams[nStream].aica_buffers[0], streams[nStream].buffer, STREAM_CHANNEL_BUFFER_SIZE/2);
	}

	if (streams[nStream].total_samples > STREAM_CHANNEL_SAMPLE_COUNT/2) {
		// If more than one buffer, prefetch the next one
		 StreamRead(nStream, streams[nStream].buffer, read_size);
	}	
}

bool AudioEngine_Initialise(void)
{
	auto init = snd_init();
	assert(init >= 0);
	snd_stream_init();
	ticks = 0;

	// Don't allocate stream channels upfront - allocate on-demand to leave channels for music streaming
	// Initialize stream structures but don't allocate AICA channels yet
	for (int i = 0; i< AUDIO_ENGINE_MAX_STREAMS; i++) {
		streams[i].mapped_ch[0] = -1;  // Allocate on-demand when stream starts
		streams[i].mapped_ch[1] = -1;  // Allocate on-demand when stream starts
		streams[i].aica_buffers[0] = snd_mem_malloc(STREAM_CHANNEL_BUFFER_SIZE);
		streams[i].aica_buffers[1] = snd_mem_malloc(STREAM_CHANNEL_BUFFER_SIZE);
		debugf("Stream %d buffers: %p, %p\n", i, (void*)streams[i].aica_buffers[0], (void*)streams[i].aica_buffers[1]);
		streams[i].fd = -1;
		streams[i].vfs_file = nullptr;
		streams[i].is_vfs = false;
		streams[i].is_memory = false;
		streams[i].mem_data = nullptr;
		streams[i].mem_size = 0;
		streams[i].mem_offset = 0;
		streams[i].vol = 255;
		streams[i].loop = 0;
		streams[i].pan[0] = AICA_PAN_LEFT;
		streams[i].pan[1] = AICA_PAN_RIGHT;
		streams[i].last_tick = ticks;
	}

	// Don't allocate SFX channels upfront - allocate on-demand to leave channels for music streaming
	// Initialize SFX channel structures but don't allocate AICA channels yet
	for (int i = 0; i < (AUDIO_ENGINE_MAX_CHANNELS); i++) {
		sfx_channels[i].mapped_ch = -1;  // Allocate on-demand in getOpenSfxChannel()
		sfx_channels[i].last_tick = ticks;
		sfx_channels[i].sfx_index = -1;
	}

	for (int i = 0; i < (AUDIO_ENGINE_MAX_SFX); i++) {
		//sfx[i].mapped_ch = snd_sfx_chn_alloc();
		//sfx[i].aica_buffer = snd_mem_malloc(AICA_MAX_SAMPLES / 2);        
		//assert(sfx[i].mapped_ch != -1);
        //assert(sfx[i].aica_buffer > 0);
		sfx[i].loop = 0;
		sfx[i].loop_offset = 0;
		sfx[i].has_offset = 0;
		sfx[i].saw_near_end = false;
		sfx[i].stereo = 0;
		sfx[i].nSfx = -1;
		sfx[i].pan = AICA_PAN_CENTER;
		sfx[i].last_tick = ticks;
	}

	
	snd_thread = std::thread([]() {
		for(;;) {
			{
				std::lock_guard<std::mutex> lk(channel_mtx);
				for (int i = 0; i < AUDIO_ENGINE_MAX_CHANNELS; i++) {
					 if (sfx_channels[i].sfx_index == -1 || sfx_channels[i].mapped_ch < 0)
						 continue;
					 
					 int sfx_idx = sfx_channels[i].sfx_index;
						uint16_t channel_pos = (g2_read_32(SPU_RAM_UNCACHED_BASE + AICA_CHANNEL(sfx_channels[i].mapped_ch) + offsetof(aica_channel_t, pos)) & 0xffff);
					 
					 // Check if non-looping sound has finished
					 if (!sfx[sfx_idx].loop) {
						 // Mark that we've started playing (to detect wrap-around)
						 if (channel_pos > 0 && !sfx[sfx_idx].has_offset) {
							 sfx[sfx_idx].has_offset = true;
						 }
						 // Only treat as finished when playhead has reached near end then wrapped to 0
						 // (avoids false "finished" if hardware reports 0 mid-playback)
						 uint32_t near_end = (sfx[sfx_idx].total_samples > 256)
							 ? (sfx[sfx_idx].total_samples - 256) : (sfx[sfx_idx].total_samples / 2);
						 if (channel_pos >= near_end)
							 sfx[sfx_idx].saw_near_end = true;
						 if (sfx[sfx_idx].has_offset && sfx[sfx_idx].saw_near_end && channel_pos == 0) {
							 // Sound finished - stop channel but keep it allocated for reuse
							 aica_stop_chn(sfx_channels[i].mapped_ch);
							 sfx_channels[i].sfx_index = -1;  // Mark slot as free, but keep channel allocated
							 continue;
						 }
					 }
					 // Handle looping sounds
					 else if (sfx[sfx_idx].loop && !sfx[sfx_idx].in_hnd_loop) {
						 // Double-check loop flag is still set (might have been cleared by Stop)
						 if (!sfx[sfx_idx].loop || sfx[sfx_idx].loop_offset == 0) {
							 continue;
						 }
						 verbosef("SFX %d pos: %d, %d", i, channel_pos, sfx[sfx_idx].total_samples);
						if(channel_pos) {
							 sfx[sfx_idx].has_offset = true;
						}
						 if (channel_pos >= sfx[sfx_idx].total_samples || (sfx[sfx_idx].has_offset && channel_pos == 0)) {
							 sfx[sfx_idx].in_hnd_loop = true;
							 debugf("Starting loop section: for sfx_%d_loop.wav\n", sfx[sfx_idx].nSfx);
							snd_sfx_stop(sfx_channels[i].mapped_ch);
							
							// Only loop if loop_offset is valid (non-zero)
							if (sfx[sfx_idx].loop_offset > 0 && sfx[sfx_idx].loop_offset < sfx[sfx_idx].total_samples * 2) {
								aica_play_chn(sfx_channels[i].mapped_ch, 
									 sfx[sfx_idx].total_samples - sfx[sfx_idx].loop_offset, 
									 sfx[sfx_idx].aica_buffer + (sfx[sfx_idx].loop_offset / 2), 
									 sfx[sfx_idx].type, 
									 sfx[sfx_idx].vol, 
									 sfx[sfx_idx].pan, 
									1,
									 sfx[sfx_idx].rate);
							} else {
								// Invalid loop_offset, stop the sound instead
								aica_stop_chn(sfx_channels[i].mapped_ch);
								sfx_channels[i].sfx_index = -1;  // Mark slot as free, but keep channel allocated
								sfx[sfx_idx].loop = false;
							}
						}
					}
				}
			}

			for (int i = 0; i< AUDIO_ENGINE_MAX_STREAMS; i++) {
				int do_read = 0;
				{
					std::lock_guard<std::mutex> lk(channel_mtx);
					if (streams[i].playing && streams[i].mapped_ch[0] >= 0) {
						stream_loop:
						// Calculate samples per buffer half based on format
						uint32_t samples_per_half;
						if(streams[i].type == AICA_SM_ADPCM_LS) {
							// ADPCM: 4 bits per sample = 2 samples per byte
							samples_per_half = STREAM_CHANNEL_BUFFER_SIZE * 2 / 2; // *2 for ADPCM, /2 for half buffer
						} else if(streams[i].type == AICA_SM_16BIT) {
							// 16-bit PCM: 2 bytes per sample
							samples_per_half = (STREAM_CHANNEL_BUFFER_SIZE / 2) / 2; // /2 for bytes->samples, /2 for half buffer
						} else {
							// 8-bit PCM: 1 byte per sample
							samples_per_half = (STREAM_CHANNEL_BUFFER_SIZE / 2) / 1; // /2 for half buffer, /1 for bytes->samples
						}
						
						// get channel pos
						uint32_t channel_pos = g2_read_32(SPU_RAM_UNCACHED_BASE + AICA_CHANNEL(streams[i].mapped_ch[0]) + offsetof(aica_channel_t, pos)) & 0xffff;
						uint32_t logical_pos = channel_pos;
						if (logical_pos > samples_per_half) {
							logical_pos -= samples_per_half;
						}
						//verbosef("Stream %d pos: %d, log: %d, rem: %d, total: %d\n", i, channel_pos, logical_pos, streams[i].played_samples, streams[i].total_samples);
			
						// Calculate how many samples we've actually played so far
						uint32_t current_played = streams[i].played_samples + logical_pos;
						bool can_refill = (current_played + samples_per_half) < streams[i].total_samples;
						bool can_fetch = (current_played + samples_per_half + samples_per_half + samples_per_half) < streams[i].total_samples;
						// Debug: log if can_fetch becomes false unexpectedly
						if (!can_fetch && current_played < streams[i].total_samples / 2) {
							debugf("Stream %d: can_fetch=false but only %d/%d samples played (samples_per_half=%d)\n", 
								i, current_played, streams[i].total_samples, samples_per_half);
						}
						// copy over data if needed from staging
						if (channel_pos >= samples_per_half && !streams[i].next_is_upper_half) {
							streams[i].next_is_upper_half = true;
							if (can_refill) { // could we need a refill?
								verbosef("Filling channel %d with lower half\n", i);
								// fill lower half
								if (streams[i].stereo) {
									snd_adpcm_split((uint32_t*)streams[i].buffer, (uint32_t*)sfx_buffer, (uint32_t*)sfx_buffer + STREAM_STAGING_READ_SIZE_MONO, STREAM_STAGING_READ_SIZE_STEREO);
									spu_memload_sq(streams[i].aica_buffers[0], (uint32_t*)sfx_buffer, STREAM_STAGING_READ_SIZE_STEREO/2);
									spu_memload_sq(streams[i].aica_buffers[1], (uint32_t*)sfx_buffer + STREAM_STAGING_READ_SIZE_MONO, STREAM_STAGING_READ_SIZE_STEREO/2);
								}		
								else {
									spu_memload_sq(streams[i].aica_buffers[0], streams[i].buffer, STREAM_CHANNEL_BUFFER_SIZE/2);
								}								
								// queue next read to staging if any
								if (can_fetch) {
									do_read = streams[i].stereo ? STREAM_STAGING_READ_SIZE_STEREO : STREAM_STAGING_READ_SIZE_MONO;
								}
							}
							assert(streams[i].first_refill == false);
							streams[i].played_samples += samples_per_half;
						} else if (channel_pos < samples_per_half && streams[i].next_is_upper_half) {
							streams[i].next_is_upper_half = false;
							if (can_refill) { // could we need a refill?
								verbosef("Filling channel %d with upper half\n", i);
								if (streams[i].stereo) {
									snd_adpcm_split((uint32_t*)streams[i].buffer, (uint32_t*)sfx_buffer, (uint32_t*)sfx_buffer + STREAM_STAGING_READ_SIZE_MONO, STREAM_STAGING_READ_SIZE_STEREO);
									spu_memload_sq(streams[i].aica_buffers[0] + STREAM_CHANNEL_BUFFER_SIZE/2, (uint32_t*)sfx_buffer, STREAM_STAGING_READ_SIZE_STEREO/2);
									spu_memload_sq(streams[i].aica_buffers[1] + STREAM_CHANNEL_BUFFER_SIZE/2, (uint32_t*)sfx_buffer + STREAM_STAGING_READ_SIZE_MONO, STREAM_STAGING_READ_SIZE_STEREO/2);
								}		
								else {
									spu_memload_sq(streams[i].aica_buffers[0] + STREAM_CHANNEL_BUFFER_SIZE/2, streams[i].buffer, STREAM_CHANNEL_BUFFER_SIZE/2);
								}									
								// queue next read to staging, if any
								if (can_fetch) {
									do_read = streams[i].stereo ? STREAM_STAGING_READ_SIZE_STEREO : STREAM_STAGING_READ_SIZE_MONO;
								}
							}
							if (streams[i].first_refill) {
								streams[i].first_refill = false;
							} else {
								streams[i].played_samples += samples_per_half;
							}
						}
						// if end of file, stop
						// Use >= instead of > to ensure we stop exactly at the end
						uint32_t total_played = streams[i].played_samples + logical_pos;
						if (total_played >= streams[i].total_samples) {
							// Debug: log when we hit end condition
							debugf("Stream %d end check: played_samples=%d, logical_pos=%d, total_played=%d, total_samples=%d, samples_per_half=%d\n",
								i, streams[i].played_samples, logical_pos, total_played, streams[i].total_samples, samples_per_half);
							// Check loop flag again (might have been cleared by Stop)
							// Only loop if loop flag is true AND loop_offset is non-zero
							if(streams[i].loop && streams[i].loop_offset > 0 && streams[i].playing) {
								debugf("Auto looping stream: %d -> {%d, %d}, played: %d + pos: %d >= total: %d, loop_offset: %d\n", i, streams[i].mapped_ch[0], streams[i].mapped_ch[1], streams[i].played_samples, logical_pos, streams[i].total_samples, streams[i].loop_offset);
							 if(streams[i].is_memory) {
								 streams[i].mem_offset = streams[i].loop_offset;
							 } else if(streams[i].is_vfs && streams[i].vfs_file) {
								 FS_Seek((dc_file_t *)streams[i].vfs_file, streams[i].file_data_offset + streams[i].loop_offset, SEEK_SET);
							 } else {
								fs_seek(streams[i].fd, streams[i].file_data_offset + streams[i].loop_offset, SEEK_SET);
							 }
								streams[i].played_samples = streams[i].loop_offset / (streams[i].stereo ? 2 : 1);  // Reset to loop start
								AudioEngine_LoadFirstChunk(i);
								streams[i].next_is_upper_half = true;
								do_read = 0;
							}
							else {
								// stop channel (no loop, or loop_offset is 0)
								debugf("Auto stopping stream: %d -> {%d, %d}, played: %d + pos: %d >= total: %d, loop: %d, loop_offset: %d\n", i, streams[i].mapped_ch[0], streams[i].mapped_ch[1], streams[i].played_samples, logical_pos, streams[i].total_samples, streams[i].loop, streams[i].loop_offset);
								if(streams[i].mapped_ch[0] >= 0) {
									aica_stop_chn(streams[i].mapped_ch[0]);
									streams[i].mapped_ch[0] = -1;  // Free channel for music streaming
								}
								if(streams[i].mapped_ch[1] >= 0) {
									aica_stop_chn(streams[i].mapped_ch[1]);
									streams[i].mapped_ch[1] = -1;  // Free channel for music streaming
								}
								streams[i].playing = false;
							}
						}
					}
					
					if (do_read) {
						 if(streams[i].is_memory) {
							 debugf("Queueing stream read: %d (memory), buffer: %p, size: %d, offset: %d\n", i, streams[i].buffer, do_read, streams[i].mem_offset);
						 } else if(streams[i].is_vfs && streams[i].vfs_file) {
							 debugf("Queueing stream read: %d (VFS), file: %p, buffer: %p, size: %d, tell: %d\n", i, streams[i].vfs_file, streams[i].buffer, do_read, FS_Tell((dc_file_t *)streams[i].vfs_file));
						 } else {
							 debugf("Queueing stream read: %d, file: %d, buffer: %p, size: %d, tell: %d\n", i, streams[i].fd, streams[i].buffer, do_read, fs_tell(streams[i].fd));
						 }
						 StreamRead(i, streams[i].buffer, do_read);
					}
				}
			}
			
			++ticks;

			thd_sleep(50);
		}
	});

	return true;
}

int getSfxChannelIndex(int nStream) {
	for(int i = 0; i < AUDIO_ENGINE_MAX_CHANNELS; i++) {
		if(sfx_channels[i].sfx_index == nStream) {
			return i;
		}
	}

	return -1;
}

int AudioEngine_Stop(int nStream) {
	if(nStream < AUDIO_ENGINE_MAX_STREAMS) {
		debugf("Stopping Stream: %d\n", nStream);
		if(!streams[nStream].fd && !streams[nStream].vfs_file && !streams[nStream].is_memory) {
			return 0;
		}

		if(streams[nStream].mapped_ch[0] >= 0) {
			aica_stop_chn(streams[nStream].mapped_ch[0]);
			streams[nStream].mapped_ch[0] = -1;  // Free channel for music streaming
		}
		if(streams[nStream].mapped_ch[1] >= 0) {
			aica_stop_chn(streams[nStream].mapped_ch[1]);
			streams[nStream].mapped_ch[1] = -1;  // Free channel for music streaming
		}
		streams[nStream].playing = false;
		streams[nStream].loop = false;  // Clear loop flag when stopping
		// Don't reset played_samples - preserve it so we can detect if stream ended naturally
		// Channels are freed so they're immediately available for music streaming
	}
	else {
		nStream -= AUDIO_ENGINE_MAX_STREAMS; // SFX OFFSET
		assert(nStream < AUDIO_ENGINE_MAX_SFX);

		int sfx_channel = getSfxChannelIndex(nStream);
		 if(sfx_channel < 0) {
			 return 0;
		 }

		if(sfx[nStream].nSfx == -1) {
			return 0;
		}

		if(sfx_channels[sfx_channel].mapped_ch >= 0) {
			aica_stop_chn(sfx_channels[sfx_channel].mapped_ch);
			// Keep channel allocated for reuse - only free when evicting LRU
		}
		 sfx_channels[sfx_channel].sfx_index = -1;
		 sfx[nStream].has_offset = false;
		 sfx[nStream].saw_near_end = false;
		 sfx[nStream].in_hnd_loop = false;
		 sfx[nStream].loop = false;  // Clear loop flag when stopping
	}

	return nStream;
}

int AudioEngine_Unload(int nStream) {
	if(nStream < AUDIO_ENGINE_MAX_STREAMS) {
		debugf("Stopping Stream: %d\n", nStream);

		if(streams[nStream].mapped_ch[0] >= 0) {
			aica_stop_chn(streams[nStream].mapped_ch[0]);
		}
		if(streams[nStream].mapped_ch[1] >= 0) {
			aica_stop_chn(streams[nStream].mapped_ch[1]);
		}
		streams[nStream].playing = false;
		streams[nStream].loop = false;  // Clear loop flag when unloading
		// Free channels when unloading so they're available for music streaming
		// Note: KOS doesn't have snd_sfx_chn_free, channels are automatically freed when stopped
		streams[nStream].mapped_ch[0] = -1;
		streams[nStream].mapped_ch[1] = -1;
		 
		 if(streams[nStream].is_memory) {
			 // Memory stream - just clear it
			 streams[nStream].mem_data = nullptr;
			 streams[nStream].mem_size = 0;
			 streams[nStream].mem_offset = 0;
			 streams[nStream].is_memory = false;
		 } else if(streams[nStream].is_vfs && streams[nStream].vfs_file) {
			 // VFS file stream - close VFS handle
			 FS_Close((dc_file_t *)streams[nStream].vfs_file);
			 streams[nStream].vfs_file = nullptr;
			 streams[nStream].is_vfs = false;
		 } else if(streams[nStream].fd >= 0) {
			 // KOS file stream - close KOS handle
			 fs_close(streams[nStream].fd);
			 streams[nStream].fd = -1;
		 }
	}
	else {
		nStream -= AUDIO_ENGINE_MAX_STREAMS; // SFX OFFSET
		assert(nStream < AUDIO_ENGINE_MAX_SFX);

		if(sfx[nStream].nSfx == -1) {
			return 0;  // Already unloaded
		}

		int sfx_channel = getSfxChannelIndex(nStream);
		if(sfx_channel >= 0) {
			if(sfx_channels[sfx_channel].mapped_ch >= 0) {
				aica_stop_chn(sfx_channels[sfx_channel].mapped_ch);
				// Keep channel allocated for reuse - only free when evicting LRU
			}
			sfx_channels[sfx_channel].sfx_index = -1; // Unmap AICA Channel -> SFX
		}

		if(sfx[nStream].aica_buffer) {
			snd_mem_free(sfx[nStream].aica_buffer);  // Release SRAM buffer
			sfx[nStream].aica_buffer = 0;
		}
		sfx[nStream].nSfx = -1; // Release SFX
		sfx[nStream].loop = false;  // Clear loop flag when unloading
		sfx[nStream].has_offset = false;
		sfx[nStream].saw_near_end = false;
		sfx[nStream].in_hnd_loop = false;
	}

	return nStream;
}

int AudioEngine_GetOpenStreamIndex(const char * fname, file_t fd) {
	// 1. Check for Duplicate File Name
	size_t fileNameLength = std::min(strlen(fname), (size_t)128);
    for(int i = 0; i < AUDIO_ENGINE_MAX_STREAMS; i++) {
        if (strncmp(fname, streams[i].fname, fileNameLength) == 0){
			// Close previous handle (VFS or KOS)
			if(streams[i].is_vfs && streams[i].vfs_file) {
				FS_Close((dc_file_t *)streams[i].vfs_file);
			} else if(streams[i].fd >= 0) {
				fs_close(streams[i].fd);
			}
			debugf("AudioEngine: File Already In Stream Cache! %i, %s, %s\n", i, fname, streams[i].fname);
            return i;
        }
    }
	// 2. Check for Free Index
    for(int i = 0; i < AUDIO_ENGINE_MAX_STREAMS; i++) {
        if(streams[i].fd == -1 && (!streams[i].is_vfs || !streams[i].vfs_file) && !streams[i].is_memory) {
            return i;
        }
    }
	// 3. Force Close The Stream Which Was Accessed The Least Recently
	uint32_t last_tick = streams[0].last_tick;
	int index = 0;
    for(int i = 0; i < AUDIO_ENGINE_MAX_STREAMS; i++) {
        if(streams[i].last_tick < last_tick) {
            index = i;
			last_tick = streams[i].last_tick;
        }
    }

	return AudioEngine_Unload(index);
}

int AudioEngine_GetOpenSFXIndex(const char * fname) {
	uint32_t last_tick = sfx[0].last_tick;
	// 1. Check for Duplicate File Name
	size_t fileNameLength = std::min(strlen(fname), (size_t)128);
    for(int i = 0; i < AUDIO_ENGINE_MAX_SFX; i++) {
        if (strncmp(fname, sfx[i].fname, fileNameLength) == 0){
			debugf("AudioEngine: File Already In SFX Cache! %i, %s, %s\n", i, fname, sfx[i].fname);
            return i;
        }
    }
	// 2. Check for Free Index
    for(int i = 0; i < AUDIO_ENGINE_MAX_SFX; i++) {
        if(sfx[i].nSfx == -1) {
            return i;
        }
    }
	// 3. Force Close The Stream Which Was Accessed The Least Recently
	int index = 0;
    for(int i = 0; i < AUDIO_ENGINE_MAX_SFX; i++) {
        if(sfx[i].last_tick < last_tick) {
            index = i;
			last_tick = sfx[i].last_tick;
        }
    }

	return AudioEngine_Unload(index + AUDIO_ENGINE_MAX_STREAMS);
}

bool AudioEngine_ParseWaveHeader(file_t fd, WavHeader * hdr) {
	uint8_t header_buffer[MAX_WAVE_HEADER_SIZE];
	uint8_t valid_header = 0;
    // Read the RIFF header.
	fs_seek(fd, 0, SEEK_SET);

	fs_read(fd, header_buffer, MAX_WAVE_HEADER_SIZE);

	if(!(header_buffer[0] == 'R' && header_buffer[1] == 'I' && header_buffer[2] == 'F' && header_buffer[3] == 'F')) {
		return 0;
	}

	for(int i = 0; i < MAX_WAVE_HEADER_SIZE - (sizeof(WaveFmtHeader)); i++) {
		if(header_buffer[i] == 'f' && header_buffer[i + 1] == 'm' && header_buffer[i + 2] == 't' && header_buffer[i + 3] == ' ') {
			memcpy(&hdr->fmtHeader, &header_buffer[i], sizeof(WaveFmtHeader));
			++valid_header;
			break;
		}
	}

	for(int i = 0; i < MAX_WAVE_HEADER_SIZE - (sizeof(WaveChunkHeader)); i++) {
		if(header_buffer[i] == 'd' && header_buffer[i + 1] == 'a' && header_buffer[i + 2] == 't' && header_buffer[i + 3] == 'a') {
			memcpy(&hdr->chunkHeader, &header_buffer[i], sizeof(WaveChunkHeader));
			fs_seek(fd, i, SEEK_SET);
			++valid_header;
			break;
		}
	}	

    return valid_header == 2;
}

#ifdef __cplusplus
extern "C" {
#endif
bool AudioEngine_ParseWaveHeader_VFS(void *vfs_file_ptr, struct WavHeader * hdr) {
	dc_file_t *vfs_file = (dc_file_t *)vfs_file_ptr;
	uint8_t header_buffer[MAX_WAVE_HEADER_SIZE];
	uint8_t valid_header = 0;
    // Read the RIFF header.
	FS_Seek(vfs_file, 0, SEEK_SET);

	FS_Read(vfs_file, header_buffer, MAX_WAVE_HEADER_SIZE);

	if(!(header_buffer[0] == 'R' && header_buffer[1] == 'I' && header_buffer[2] == 'F' && header_buffer[3] == 'F')) {
		return 0;
	}

	for(unsigned int i = 0; i < MAX_WAVE_HEADER_SIZE - (sizeof(WaveFmtHeader)); i++) {
		if(header_buffer[i] == 'f' && header_buffer[i + 1] == 'm' && header_buffer[i + 2] == 't' && header_buffer[i + 3] == ' ') {
			memcpy(&hdr->fmtHeader, &header_buffer[i], sizeof(WaveFmtHeader));
			++valid_header;
			break;
		}
	}

	for(unsigned int i = 0; i < MAX_WAVE_HEADER_SIZE - (sizeof(WaveChunkHeader)); i++) {
		if(header_buffer[i] == 'd' && header_buffer[i + 1] == 'a' && header_buffer[i + 2] == 't' && header_buffer[i + 3] == 'a') {
			memcpy(&hdr->chunkHeader, &header_buffer[i], sizeof(WaveChunkHeader));
			FS_Seek(vfs_file, i, SEEK_SET);
			++valid_header;
			break;
		}
	}	

    return valid_header == 2;
}
#ifdef __cplusplus
}
#endif

int AudioEngine_Load(const char * fname, uint32_t seek_bytes_aligned)
{
	file_t f = fs_open(fname, O_RDONLY);
	assert(f >= 0 );
	WavHeader hdr;
    bool validHeader = AudioEngine_ParseWaveHeader(f, &hdr);
	assert(validHeader > 0);

	debugf("AudioEngine_Load: %s, %i\n",fname, hdr.chunkHeader.size);

    int total_samples = (int)((float)hdr.chunkHeader.size  / (((float)(hdr.fmtHeader.bitsPerSample) / 8) * (float)hdr.fmtHeader.numChannels));

    if(total_samples > AICA_MAX_SAMPLES)
	{
        int nStream = AudioEngine_GetOpenStreamIndex(fname, f);
        if(nStream < 0) {
            debugf("PreloadStreamedFile Error: No Available Streams!\n");
            fs_close(f);
            return nStream;
        }

        assert( nStream < AUDIO_ENGINE_MAX_STREAMS );

	    debugf("PreloadStreamedFile(%p, %d) is %s\n", f, nStream, fname);

		std::lock_guard<std::mutex> lk(channel_mtx);

		strncpy(streams[nStream].fname, fname, 127);
    	streams[nStream].fname[127] = '\0';

		// Stop if playing
		// Keep in sync with StopStreamedFile
		if (streams[nStream].playing) {
			streams[nStream].playing = false;
			if(streams[nStream].mapped_ch[0] >= 0) {
				aica_stop_chn(streams[nStream].mapped_ch[0]);
			}
			if(streams[nStream].mapped_ch[1] >= 0) {
				aica_stop_chn(streams[nStream].mapped_ch[1]);
			}
		}

		if (streams[nStream].fd >= 0) {
            // close prevoius handle
			fs_close(streams[nStream].fd);
		}
		streams[nStream].fd = -1;

		streams[nStream].rate = hdr.fmtHeader.sampleRate;
		streams[nStream].stereo = hdr.fmtHeader.numChannels == 2;
		streams[nStream].fd = f;
		streams[nStream].playing = false;
		streams[nStream].total_samples = total_samples;
		streams[nStream].played_samples = 0;
		streams[nStream].next_is_upper_half = true;
		streams[nStream].first_refill = true;
        streams[nStream].type = hdr.fmtHeader.bitsPerSample == 16 ? AICA_SM_16BIT : hdr.fmtHeader.bitsPerSample == 8  ? AICA_SM_8BIT : AICA_SM_ADPCM_LS;
		streams[nStream].last_tick = ticks;

		debugf("Preload Streamed File: %s: stream: %d, freq: %d, chans: %d, byte size: %d, played samples: %d, total samples: %d, blockAlign: %d\n", 
            fname, nStream, hdr.fmtHeader.sampleRate, hdr.fmtHeader.numChannels, hdr.chunkHeader.size, streams[nStream].played_samples, streams[nStream].total_samples, hdr.fmtHeader.blockAlign);

		// How to avoid the lock?
		if (seek_bytes_aligned) {
			streams[nStream].played_samples = seek_bytes_aligned * (streams[nStream].stereo ? 1 : 2);
			debugf("Seeking aligned to: %d, played_samples: %d\n", seek_bytes_aligned, streams[nStream].played_samples);
			fs_seek(streams[nStream].fd, 2048 + seek_bytes_aligned, SEEK_SET);
		}

		streams[nStream].file_data_offset = fs_tell(f);

		// Stage to memory
		AudioEngine_LoadFirstChunk(nStream);

        verbosef("PreloadStreamedFile: %p - %s, %d, %d, %d\n", f, fname, streams[nStream].rate, streams[nStream].stereo, streams[nStream].type);

        return nStream;
	}
    else {
        int nStream = AudioEngine_GetOpenSFXIndex(fname);
        if(nStream < 0) {
            debugf("PreloadStreamedFile Error: No Available Streams!\n");
            fs_close(f);
            return nStream;
        }

        assert( nStream < AUDIO_ENGINE_MAX_SFX );

	    debugf("Preload SFX(%p, %d) is %s\n", f, nStream, fname);

		std::lock_guard<std::mutex> lk(channel_mtx);


		strncpy(sfx[nStream].fname, fname, 127);
    	sfx[nStream].fname[127] = '\0';

		sfx[nStream].rate = hdr.fmtHeader.sampleRate;
		sfx[nStream].stereo = hdr.fmtHeader.numChannels == 2;
		sfx[nStream].nSfx = nStream;
        sfx[nStream].type = hdr.fmtHeader.bitsPerSample == 16 ? AICA_SM_16BIT : hdr.fmtHeader.bitsPerSample == 8  ? AICA_SM_8BIT : AICA_SM_ADPCM_LS;
        sfx[nStream].total_samples = total_samples;
		sfx[nStream].last_tick = ticks;

		debugf("Preload SFX: %s: stream: %d, freq: %d, chans: %d, byte size: %d, total samples: %d\n", 
            fname, nStream, hdr.fmtHeader.sampleRate, hdr.fmtHeader.numChannels, hdr.chunkHeader.size, sfx[nStream].total_samples);

		// Stage to memory
        uint32_t sfx_size = fs_total(f) - fs_tell(f);
		sfx[nStream].aica_buffer = snd_mem_malloc(sfx_size); 
		assert(sfx[nStream].aica_buffer > 0);
		fs_read(f, sfx_buffer, sfx_size);
		spu_memload_sq(sfx[nStream].aica_buffer, sfx_buffer, sfx_size);
		if (sfx[nStream].stereo) {
			//spu_memload_sq(sfx[nStream].aica_buffers[1], sfx[nStream].buffer + STREAM_STAGING_READ_SIZE_MONO, STREAM_CHANNEL_BUFFER_SIZE/2);
		}

        fs_close(f);

        verbosef("Preload SFX File: %p - %s, %d, %d, %d\n", f, fname, sfx[nStream].rate, sfx[nStream].stereo, sfx[nStream].type);

        return nStream + AUDIO_ENGINE_MAX_STREAMS; // Offset for SFX
    }    
}

int AudioEngine_LoadFromVFS(void *vfs_file_ptr, const char *fname, uint32_t seek_bytes_aligned)
{
	dc_file_t *vfs_file = (dc_file_t *)vfs_file_ptr;
	if(!vfs_file) {
		debugf("AudioEngine_LoadFromVFS: Invalid VFS file handle\n");
		return -1;
	}

	WavHeader hdr;
    bool validHeader = AudioEngine_ParseWaveHeader_VFS(vfs_file, &hdr);
	if(!validHeader) {
		debugf("AudioEngine_LoadFromVFS: Failed to parse WAV header for %s\n", fname);
		return -1;
	}

	debugf("AudioEngine_LoadFromVFS: %s, %i\n", fname, hdr.chunkHeader.size);

	// Check audio format - format 17 is WAVE_FORMAT_ADPCM
	bool is_adpcm = (hdr.fmtHeader.audioFormat == 17 || hdr.fmtHeader.audioFormat == 2);
	
	// Calculate total samples based on format
	int total_samples;
	if(is_adpcm && hdr.fmtHeader.blockAlign > 0)
	{
		// ADPCM: samples per block = (blockAlign - 4) * 2 / numChannels + 2
		// Total samples = (data_size / blockAlign) * samples_per_block
		int samples_per_block = ((hdr.fmtHeader.blockAlign - 4) * 2) / hdr.fmtHeader.numChannels + 2;
		int num_blocks = hdr.chunkHeader.size / hdr.fmtHeader.blockAlign;
		total_samples = num_blocks * samples_per_block;
	}
	else
	{
		// PCM format
		total_samples = (int)((float)hdr.chunkHeader.size  / (((float)(hdr.fmtHeader.bitsPerSample) / 8) * (float)hdr.fmtHeader.numChannels));
	}

    if(total_samples > AICA_MAX_SAMPLES)
	{
        // Large sound - needs streaming
        int nStream = AudioEngine_GetOpenStreamIndex(fname, -1); // Pass -1 since we're using VFS
        if(nStream < 0) {
            debugf("AudioEngine_LoadFromVFS Error: No Available Streams!\n");
            return -1;
        }

        assert( nStream < AUDIO_ENGINE_MAX_STREAMS );

	    debugf("AudioEngine_LoadFromVFS: PreloadStreamedFile(%p, %d) is %s\n", vfs_file, nStream, fname);

		std::lock_guard<std::mutex> lk(channel_mtx);

		strncpy(streams[nStream].fname, fname, 127);
    	streams[nStream].fname[127] = '\0';

		// Stop if playing
		if (streams[nStream].playing) {
			streams[nStream].playing = false;
			if(streams[nStream].mapped_ch[0] >= 0) {
				aica_stop_chn(streams[nStream].mapped_ch[0]);
			}
			if(streams[nStream].mapped_ch[1] >= 0) {
				aica_stop_chn(streams[nStream].mapped_ch[1]);
			}
		}

		// Close previous handles
		if (streams[nStream].fd >= 0) {
			fs_close(streams[nStream].fd);
			streams[nStream].fd = -1;
		}
		if (streams[nStream].vfs_file) {
			FS_Close((dc_file_t *)streams[nStream].vfs_file);
			streams[nStream].vfs_file = nullptr;
		}

		// Set up VFS file handle
		streams[nStream].vfs_file = vfs_file;
		streams[nStream].is_vfs = true;
		streams[nStream].fd = -1;
		streams[nStream].rate = hdr.fmtHeader.sampleRate;
		streams[nStream].stereo = hdr.fmtHeader.numChannels == 2;
		streams[nStream].playing = false;
		streams[nStream].total_samples = total_samples;
		// Reset played_samples when loading - if stream ended naturally, AudioEngine_Play will prevent restart
		streams[nStream].played_samples = 0;
		streams[nStream].next_is_upper_half = true;
		streams[nStream].first_refill = true;
		streams[nStream].loop = false;  // Initialize loop flag to false
		streams[nStream].loop_offset = 0;  // Initialize loop offset to 0
		// Determine format based on audioFormat field, not just bitsPerSample
		if(is_adpcm)
		{
			streams[nStream].type = AICA_SM_ADPCM_LS;
		}
		else
		{
			streams[nStream].type = hdr.fmtHeader.bitsPerSample == 16 ? AICA_SM_16BIT : hdr.fmtHeader.bitsPerSample == 8  ? AICA_SM_8BIT : AICA_SM_ADPCM_LS;
		}
		streams[nStream].last_tick = ticks;

		debugf("AudioEngine_LoadFromVFS: Preload Streamed File: %s: stream: %d, freq: %d, chans: %d, byte size: %d, played samples: %d, total samples: %d, blockAlign: %d\n", 
            fname, nStream, hdr.fmtHeader.sampleRate, hdr.fmtHeader.numChannels, hdr.chunkHeader.size, streams[nStream].played_samples, streams[nStream].total_samples, hdr.fmtHeader.blockAlign);

		// Handle seek_bytes_aligned
		if (seek_bytes_aligned) {
			streams[nStream].played_samples = seek_bytes_aligned * (streams[nStream].stereo ? 1 : 2);
			debugf("Seeking aligned to: %d, played_samples: %d\n", seek_bytes_aligned, streams[nStream].played_samples);
			FS_Seek(vfs_file, 2048 + seek_bytes_aligned, SEEK_SET);
		}

		streams[nStream].file_data_offset = FS_Tell(vfs_file);

		// Stage to memory
		AudioEngine_LoadFirstChunk(nStream);

        verbosef("AudioEngine_LoadFromVFS: PreloadStreamedFile: %p - %s, %d, %d, %d\n", vfs_file, fname, streams[nStream].rate, streams[nStream].stereo, streams[nStream].type);

        return nStream;
	}
    else {
        // Small sound - load as SFX from VFS
        int nStream = AudioEngine_GetOpenSFXIndex(fname);
        if(nStream < 0) {
            debugf("AudioEngine_LoadFromVFS Error: No Available SFX Slots!\n");
            return -1;
        }

        assert( nStream < AUDIO_ENGINE_MAX_SFX );

	    debugf("AudioEngine_LoadFromVFS: Preload SFX(%p, %d) is %s\n", vfs_file, nStream, fname);

		std::lock_guard<std::mutex> lk(channel_mtx);

		strncpy(sfx[nStream].fname, fname, 127);
    	sfx[nStream].fname[127] = '\0';

		sfx[nStream].rate = hdr.fmtHeader.sampleRate;
		sfx[nStream].stereo = hdr.fmtHeader.numChannels == 2;
		sfx[nStream].nSfx = nStream;
		// Determine format based on audioFormat field, not just bitsPerSample
		if(is_adpcm)
		{
			sfx[nStream].type = AICA_SM_ADPCM_LS;
		}
		else
		{
			sfx[nStream].type = hdr.fmtHeader.bitsPerSample == 16 ? AICA_SM_16BIT : hdr.fmtHeader.bitsPerSample == 8  ? AICA_SM_8BIT : AICA_SM_ADPCM_LS;
		}
        sfx[nStream].total_samples = total_samples;
		sfx[nStream].last_tick = ticks;

		debugf("AudioEngine_LoadFromVFS: Preload SFX: %s: stream: %d, freq: %d, chans: %d, byte size: %d, total samples: %d\n", 
            fname, nStream, hdr.fmtHeader.sampleRate, hdr.fmtHeader.numChannels, hdr.chunkHeader.size, sfx[nStream].total_samples);

		// Read sample data from VFS file
		fs_offset_t current_pos = FS_Tell(vfs_file);
		fs_offset_t file_size = FS_FileLength(vfs_file);
		uint32_t sfx_size = (uint32_t)(file_size - current_pos);
		
		sfx[nStream].aica_buffer = snd_mem_malloc(sfx_size); 
		if(sfx[nStream].aica_buffer == 0) {
			debugf("AudioEngine_LoadFromVFS: Failed to allocate AICA buffer for %s (size=%d) - out of sound RAM!\n", fname, sfx_size);
			sfx[nStream].nSfx = -1;
			return -1;
		}
		
		// Ensure we don't overflow sfx_buffer
		if(sfx_size > sizeof(sfx_buffer)) {
			debugf("AudioEngine_LoadFromVFS: SFX size %d exceeds buffer size %d for %s\n", sfx_size, sizeof(sfx_buffer), fname);
			snd_mem_free(sfx[nStream].aica_buffer);
			sfx[nStream].aica_buffer = 0;
			sfx[nStream].nSfx = -1;
			return -1;
		}
		
		// Read data in chunks if needed
		uint32_t remaining = sfx_size;
		uint8_t *dst = sfx_buffer;
		while(remaining > 0) {
			uint32_t to_read = (remaining > sizeof(sfx_buffer)) ? sizeof(sfx_buffer) : remaining;
			fs_offset_t read = FS_Read(vfs_file, dst, to_read);
			if(read <= 0) break;
			dst += read;
			remaining -= (uint32_t)read;
		}
		
		// Handle format conversion (8-bit unsigned -> signed)
		if(sfx[nStream].type == AICA_SM_8BIT) {
			uint8_t* src = (uint8_t*)sfx_buffer;
			int8_t* dst = (int8_t*)sfx_buffer;
			for(uint32_t i = 0; i < sfx_size; i++) {
				dst[i] = (int8_t)((int)src[i] - 128);  // Convert 0-255 to -128 to 127
			}
		}
		
		spu_memload_sq(sfx[nStream].aica_buffer, sfx_buffer, sfx_size);

        verbosef("AudioEngine_LoadFromVFS: Preload SFX File: %p - %s, %d, %d, %d\n", vfs_file, fname, sfx[nStream].rate, sfx[nStream].stereo, sfx[nStream].type);

        return nStream + AUDIO_ENGINE_MAX_STREAMS; // Offset for SFX
    }
}

int AudioEngine_LoadFromWaveInfo(const uint8_t * sample_data, uint32_t sample_size, int sample_rate, int channels, int bits_per_sample, const char * name, const uint8_t * full_wav_data, uint32_t full_wav_size)
{
	if(!sample_data || sample_size == 0) {
		return -1;
	}

	// Calculate total samples based on format
	// ADPCM (4-bit): 2 samples per byte
	// PCM 8-bit: 1 sample per byte
	// PCM 16-bit: 1 sample per 2 bytes
	int total_samples;
	if( bits_per_sample == 4 )
	{
		// ADPCM: 4 bits per sample = 2 samples per byte
		total_samples = sample_size * 2;
	}
	else if( bits_per_sample == 8 )
	{
		// PCM 8-bit: 1 byte per sample
		total_samples = sample_size;
	}
	else if( bits_per_sample == 16 )
	{
		// PCM 16-bit: 2 bytes per sample
		total_samples = sample_size / 2;
	}
	else
	{
		// Fallback calculation
		total_samples = (int)((float)sample_size / (((float)bits_per_sample / 8) * (float)channels));
	}

	// Handle large sounds with in-memory streaming
    if(total_samples > AICA_MAX_SAMPLES)
	{
		if(!full_wav_data || full_wav_size == 0) {
			debugf("AudioEngine_LoadFromWaveInfo: Sound %s too large for SFX (%d samples > %d), full WAV data required for streaming\n", 
			name, total_samples, AICA_MAX_SAMPLES);
		return -1;
		}

		// Set up in-memory streaming
		std::lock_guard<std::mutex> lk(channel_mtx);
		
		// Find an available stream slot
		int nStream = -1;
		for(int i = 0; i < AUDIO_ENGINE_MAX_STREAMS; i++) {
			if(streams[i].fd == -1 && (!streams[i].is_memory || streams[i].mem_data == nullptr)) {
				nStream = i;
				break;
			}
		}
		if(nStream < 0) {
			// Find LRU stream
			uint32_t last_tick = streams[0].last_tick;
			nStream = 0;
			for(int i = 0; i < AUDIO_ENGINE_MAX_STREAMS; i++) {
				if(streams[i].last_tick < last_tick) {
					nStream = i;
					last_tick = streams[i].last_tick;
				}
			}
			// Stop and unload the LRU stream
			if(streams[nStream].playing) {
				if(streams[nStream].mapped_ch[0] >= 0) {
					aica_stop_chn(streams[nStream].mapped_ch[0]);
				}
				if(streams[nStream].mapped_ch[1] >= 0) {
					aica_stop_chn(streams[nStream].mapped_ch[1]);
				}
				streams[nStream].playing = false;
			}
			if(streams[nStream].is_memory) {
				// Memory stream - just clear it
				streams[nStream].mem_data = nullptr;
			} else if(streams[nStream].fd >= 0) {
				fs_close(streams[nStream].fd);
			}
		}

		// Parse WAV header to find data chunk offset
		uint32_t data_offset = 0;
		for(uint32_t i = 0; i < full_wav_size - 8; i++) {
			if(full_wav_data[i] == 'd' && full_wav_data[i+1] == 'a' && 
			   full_wav_data[i+2] == 't' && full_wav_data[i+3] == 'a') {
				data_offset = i + 8; // Skip "data" and size
				break;
			}
		}
		if(data_offset == 0) {
			debugf("AudioEngine_LoadFromWaveInfo: Could not find data chunk in WAV for %s\n", name);
			return -1;
		}

		// Set up memory stream
		strncpy(streams[nStream].fname, name, 127);
		streams[nStream].fname[127] = '\0';
		streams[nStream].is_memory = true;
		streams[nStream].mem_data = full_wav_data + data_offset;
		streams[nStream].mem_size = sample_size;
		streams[nStream].mem_offset = 0;
		streams[nStream].fd = -1;
		streams[nStream].rate = sample_rate;
		streams[nStream].stereo = channels == 2;
		streams[nStream].playing = false;
		streams[nStream].total_samples = total_samples;
		streams[nStream].played_samples = 0;
		streams[nStream].next_is_upper_half = true;
		streams[nStream].first_refill = true;
		streams[nStream].type = bits_per_sample == 16 ? AICA_SM_16BIT : bits_per_sample == 8 ? AICA_SM_8BIT : AICA_SM_ADPCM_LS;
		streams[nStream].file_data_offset = 0; // Not used for memory streams
		streams[nStream].last_tick = ticks;

		debugf("Load large sound as memory stream: %s: stream: %d, freq: %d, chans: %d, total samples: %d\n", 
			name, nStream, sample_rate, channels, total_samples);

		// Load first chunk
		AudioEngine_LoadFirstChunk(nStream);

		return nStream; // Return stream index (not offset)
	}

    int nStream = AudioEngine_GetOpenSFXIndex(name);
    if(nStream < 0) {
        debugf("AudioEngine_LoadFromWaveInfo Error: No Available SFX Slots!\n");
        return -1;
    }

    assert( nStream < AUDIO_ENGINE_MAX_SFX );

	std::lock_guard<std::mutex> lk(channel_mtx);

	// If this slot already has a buffer allocated (reused slot from LRU eviction), free it first
	if(sfx[nStream].nSfx == nStream && sfx[nStream].aica_buffer > 0) {
		// This slot is currently in use with a valid buffer - free it before reusing
		// Stop any playing instance
		int sfx_channel = getSfxChannelIndex(nStream);
		if(sfx_channel >= 0) {
			if(sfx_channels[sfx_channel].mapped_ch >= 0) {
				aica_stop_chn(sfx_channels[sfx_channel].mapped_ch);
				// Keep channel allocated for reuse - only free when evicting LRU
			}
			sfx_channels[sfx_channel].sfx_index = -1;
		}
		// Free the old buffer
		snd_mem_free(sfx[nStream].aica_buffer);
		sfx[nStream].aica_buffer = 0;
	}

	strncpy(sfx[nStream].fname, name, 127);
    sfx[nStream].fname[127] = '\0';

	sfx[nStream].rate = sample_rate;
	sfx[nStream].stereo = channels == 2;
	sfx[nStream].nSfx = nStream;
    
	// Determine format - check for ADPCM first (4-bit), then PCM formats
	// ADPCM is pre-encoded in UnAudio.cpp, AudioEngine just needs to use it
	if( bits_per_sample == 4 )
	{
		// 4-bit ADPCM format (pre-encoded in UnAudio.cpp)
		sfx[nStream].type = AICA_SM_ADPCM_LS;
	}
	else if( bits_per_sample == 8 )
	{
		sfx[nStream].type = AICA_SM_8BIT;
	}
	else if( bits_per_sample == 16 )
	{
		sfx[nStream].type = AICA_SM_16BIT;
	}
	else
	{
		// Default to 8-bit PCM instead of ADPCM for unknown bit depths
		sfx[nStream].type = AICA_SM_8BIT;
	}
	
    sfx[nStream].total_samples = total_samples;
	sfx[nStream].last_tick = ticks;

	// Allocate AICA buffer and load sample data
	// Check if sample_size is valid and not too large
	if(sample_size == 0 || sample_size > (AICA_MAX_SAMPLES * 2)) {
		debugf("AudioEngine_LoadFromWaveInfo: Invalid sample_size=%d for %s\n", sample_size, name);
		sfx[nStream].nSfx = -1; // Mark as free
		return -1;
	}
	
	sfx[nStream].aica_buffer = snd_mem_malloc(sample_size); 
	if(sfx[nStream].aica_buffer == 0) {
		debugf("AudioEngine_LoadFromWaveInfo: Failed to allocate AICA buffer for %s (size=%d) - out of sound RAM!\n", name, sample_size);
		sfx[nStream].nSfx = -1; // Mark as free
		return -1;
	}
	
	// Copy sample data directly from memory
	// Ensure we don't overflow sfx_buffer
	if(sample_size > sizeof(sfx_buffer)) {
		debugf("AudioEngine_LoadFromWaveInfo: Sample size %d exceeds buffer size %d for %s\n", sample_size, sizeof(sfx_buffer), name);
		snd_mem_free(sfx[nStream].aica_buffer);
		sfx[nStream].aica_buffer = 0;
		sfx[nStream].nSfx = -1;
		return -1;
	}
	
	// Copy sample data - format-specific handling
	if( sfx[nStream].type == AICA_SM_ADPCM_LS )
	{
		// ADPCM data is already encoded, just copy it
		memcpy(sfx_buffer, sample_data, sample_size);
	}
	else if( sfx[nStream].type == AICA_SM_8BIT )
	{
		// Convert unsigned 8-bit WAV samples (0-255) to signed 8-bit (-128 to 127)
		// WAV 8-bit PCM is unsigned, but AICA expects signed
		uint8_t* src = (uint8_t*)sample_data;
		int8_t* dst = (int8_t*)sfx_buffer;
		for(uint32_t i = 0; i < sample_size; i++)
		{
			dst[i] = (int8_t)((int)src[i] - 128);  // Convert 0-255 to -128 to 127
		}
	}
	else
	{
		// 16-bit samples are already signed, just copy
		memcpy(sfx_buffer, sample_data, sample_size);
	}
	
	spu_memload_sq(sfx[nStream].aica_buffer, sfx_buffer, sample_size);

    return nStream + AUDIO_ENGINE_MAX_STREAMS; // Offset for SFX
}

int getOpenSfxChannel() {
	// First, try to find an unused slot (sfx_index < 0) with an already-allocated channel
	// This reuses channels instead of allocating new ones
	for(int i = 0; i < AUDIO_ENGINE_MAX_CHANNELS; i++) {
		if(sfx_channels[i].sfx_index < 0 && sfx_channels[i].mapped_ch >= 0) {
			// Reuse existing channel
			debugf("getOpenSfxChannel: Reusing channel %d for SFX slot %d\n", sfx_channels[i].mapped_ch, i);
			return i;
		}
	}
	
	// Second, try to find an unused slot (sfx_index < 0) without a channel - allocate one
	for(int i = 0; i < AUDIO_ENGINE_MAX_CHANNELS; i++) {
		if(sfx_channels[i].sfx_index < 0) {
			// Slot is free but no channel allocated - allocate one now
			if(sfx_channels[i].mapped_ch < 0) {
				sfx_channels[i].mapped_ch = snd_sfx_chn_alloc();
				if(sfx_channels[i].mapped_ch < 0) {
					debugf("getOpenSfxChannel: Failed to allocate AICA channel for SFX slot %d\n", i);
					continue;  // Try next slot
				}
				debugf("getOpenSfxChannel: Allocated channel %d for SFX slot %d\n", sfx_channels[i].mapped_ch, i);
			}
			return i;
		}
	}

	// All channels in use - prefer evicting a non-looping sound that's already near end
	int index = -1;
	for (int i = 0; i < AUDIO_ENGINE_MAX_CHANNELS; i++) {
		if (sfx_channels[i].sfx_index < 0 || sfx_channels[i].mapped_ch < 0)
			continue;
		int idx = sfx_channels[i].sfx_index;
		if (sfx[idx].loop || sfx[idx].total_samples == 0)
			continue;
		uint32_t near_end = (sfx[idx].total_samples > 256)
			? (sfx[idx].total_samples - 256) : (sfx[idx].total_samples / 2);
		uint16_t channel_pos = (g2_read_32(SPU_RAM_UNCACHED_BASE + AICA_CHANNEL(sfx_channels[i].mapped_ch) + offsetof(aica_channel_t, pos)) & 0xffff);
		if (channel_pos >= near_end) {
			index = i;
			debugf("getOpenSfxChannel: Evicting nearly-finished SFX slot %d (SFX %d)\n", i, idx);
			break;
		}
	}
	// Otherwise evict LRU
	if (index < 0) {
		uint32_t last_tick = sfx_channels[0].last_tick;
		index = 0;
		for (int i = 0; i < AUDIO_ENGINE_MAX_CHANNELS; i++) {
			if (sfx_channels[i].last_tick < last_tick) {
				index = i;
				last_tick = sfx_channels[i].last_tick;
			}
		}
		debugf("getOpenSfxChannel: Freeing LRU channel %d (SFX %d)\n", index, sfx_channels[index].sfx_index);
	}

	// Stop the sound on this channel and free it
	if (sfx_channels[index].sfx_index >= 0) {
		int evicted_idx = sfx_channels[index].sfx_index;
		if (sfx_channels[index].mapped_ch >= 0) {
			aica_stop_chn(sfx_channels[index].mapped_ch);
			sfx_channels[index].mapped_ch = -1;  // Free channel for music streaming
		}
		sfx_channels[index].sfx_index = -1;
		sfx[evicted_idx].has_offset = false;
		sfx[evicted_idx].saw_near_end = false;
	}

	return index;
}

void AudioEngine_Play(int nStream, uint8_t volume, uint8_t panl, uint8_t panr, bool loop, uint32_t loop_offset)
{
    if(nStream < AUDIO_ENGINE_MAX_STREAMS) {
        std::lock_guard<std::mutex> lk(channel_mtx);
        bool was_playing = streams[nStream].playing;
        
        streams[nStream].vol = volume;
		streams[nStream].pan[0] = (streams[nStream].stereo) ? panl : ((panl + panr) >> 2);
		streams[nStream].pan[1] = panr;
		
		// If already playing, just update volume/pan without restarting
		if (was_playing) {
			// Ensure channels are allocated (should be, but be safe)
			if(streams[nStream].mapped_ch[0] < 0) {
				streams[nStream].mapped_ch[0] = snd_sfx_chn_alloc();
				if(streams[nStream].mapped_ch[0] < 0) {
					debugf("AudioEngine_Play: Failed to allocate channel for stream %d\n", nStream);
					return;
				}
			}
			if(streams[nStream].stereo && streams[nStream].mapped_ch[1] < 0) {
				streams[nStream].mapped_ch[1] = snd_sfx_chn_alloc();
				if(streams[nStream].mapped_ch[1] < 0) {
					debugf("AudioEngine_Play: Failed to allocate second channel for stereo stream %d\n", nStream);
					// Continue with mono
				}
			}
			aica_volpan_chn(streams[nStream].mapped_ch[0], streams[nStream].vol, streams[nStream].pan[0]);
			if(streams[nStream].stereo && streams[nStream].mapped_ch[1] >= 0) {
				aica_volpan_chn(streams[nStream].mapped_ch[1], streams[nStream].vol, streams[nStream].pan[1]);
			}
			return;
		}

		// Allocate channels on-demand if not already allocated
		if(streams[nStream].mapped_ch[0] < 0) {
			streams[nStream].mapped_ch[0] = snd_sfx_chn_alloc();
			if(streams[nStream].mapped_ch[0] < 0) {
				debugf("AudioEngine_Play: Failed to allocate channel for stream %d - no channels available\n", nStream);
				return;
			}
			debugf("AudioEngine_Play: Allocated channel %d for stream %d\n", streams[nStream].mapped_ch[0], nStream);
		}
		if(streams[nStream].stereo && streams[nStream].mapped_ch[1] < 0) {
			streams[nStream].mapped_ch[1] = snd_sfx_chn_alloc();
			if(streams[nStream].mapped_ch[1] < 0) {
				debugf("AudioEngine_Play: Failed to allocate second channel for stereo stream %d - using mono\n", nStream);
				// Continue with mono
			} else {
				debugf("AudioEngine_Play: Allocated channel %d for stream %d (stereo)\n", streams[nStream].mapped_ch[1], nStream);
			}
		}

		// Only set loop flag if loop_offset is valid (non-zero)
		// This prevents sounds without loop points from looping
		streams[nStream].loop = (loop && loop_offset > 0);
		streams[nStream].loop_offset = loop_offset;

		// Reset playback state when starting/restarting a stream.
		// IMPORTANT: higher-level code (e.g. spatialize/volume updates) must not call AudioEngine_Play
		// for streams that aren't playing, otherwise it will restart ended streams.
		if (streams[nStream].loop) {
			// Looping - start from loop point
			streams[nStream].played_samples = loop_offset / (streams[nStream].stereo ? 2 : 1);
			if(streams[nStream].is_memory) {
				streams[nStream].mem_offset = loop_offset;
			} else if(streams[nStream].is_vfs && streams[nStream].vfs_file) {
				FS_Seek((dc_file_t *)streams[nStream].vfs_file, streams[nStream].file_data_offset + loop_offset, SEEK_SET);
			} else if(streams[nStream].fd >= 0) {
				fs_seek(streams[nStream].fd, streams[nStream].file_data_offset + loop_offset, SEEK_SET);
			}
		} else {
			// One-shot - start from beginning
			streams[nStream].played_samples = 0;
			if(streams[nStream].is_memory) {
				streams[nStream].mem_offset = 0;
			} else if(streams[nStream].is_vfs && streams[nStream].vfs_file) {
				FS_Seek((dc_file_t *)streams[nStream].vfs_file, streams[nStream].file_data_offset, SEEK_SET);
			} else if(streams[nStream].fd >= 0) {
				fs_seek(streams[nStream].fd, streams[nStream].file_data_offset, SEEK_SET);
			}
		}
		streams[nStream].next_is_upper_half = true;
		streams[nStream].first_refill = true;

		streams[nStream].last_tick = ticks;
		
		// Load first chunk to fill buffers (after resetting file position)
		AudioEngine_LoadFirstChunk(nStream);

        debugf("StartPreloadedStreamedFile(%d) - actually starting stream, loop: %d, loop_offset: %d\n", nStream, streams[nStream].loop, streams[nStream].loop_offset);

        // Streaming uses a double buffer: we must loop the hardware buffer so the playhead wraps
        // and we can refill both halves. End-of-stream and user looping are handled in software.
        int aica_loop = 1;

        // AICA length/pos are in samples; use format-dependent length so position matches refill logic
        int stream_length_samples;
        if (streams[nStream].type == AICA_SM_ADPCM_LS) {
            stream_length_samples = STREAM_CHANNEL_SAMPLE_COUNT;  // 16384
        } else if (streams[nStream].type == AICA_SM_16BIT) {
            stream_length_samples = STREAM_CHANNEL_BUFFER_SIZE / 2;  // 4096
        } else {
            stream_length_samples = STREAM_CHANNEL_BUFFER_SIZE;  // 8192 for 8-bit
        }

        aica_play_chn(
            streams[nStream].mapped_ch[0],
            stream_length_samples,
            streams[nStream].aica_buffers[0],
            streams[nStream].type,
            streams[nStream].vol,
            streams[nStream].pan[0],
            aica_loop,
            streams[nStream].rate
        );
		if(streams[nStream].stereo) {
			aica_play_chn(
				streams[nStream].mapped_ch[1],
				stream_length_samples,
				streams[nStream].aica_buffers[1],
				streams[nStream].type,
				streams[nStream].vol,
				streams[nStream].pan[1],
				aica_loop,
				streams[nStream].rate
			);
		}

        streams[nStream].playing = true;  
    }
    else {
        nStream -= AUDIO_ENGINE_MAX_STREAMS; // SFX Offset

        assert( nStream < AUDIO_ENGINE_MAX_SFX );
        std::lock_guard<std::mutex> lk(channel_mtx);

        sfx[nStream].vol = volume;
		sfx[nStream].last_tick = ticks;
		// Channel intent: loop=true means loop (from start if loop_offset==0, else from loop point).
		sfx[nStream].loop = loop;
		sfx[nStream].loop_offset = loop ? loop_offset : 0;
		sfx[nStream].pan = (panl + panr) >> 2;

		 // Check if this SFX is already playing on a channel
		 int sfx_channel = getSfxChannelIndex(nStream);
		 if(sfx_channel >= 0) {
			 // Already playing: just update volume/pan without restarting
			 aica_snd_sfx_volume(sfx_channels[sfx_channel].mapped_ch, sfx[nStream].vol);
			 aica_snd_sfx_pan(sfx_channels[sfx_channel].mapped_ch, sfx[nStream].pan);
			 sfx_channels[sfx_channel].last_tick = ticks;
			 return;
		 }
		 
		 // Not playing: start it
		 sfx[nStream].has_offset = false; // Reset for new playback
		 sfx[nStream].saw_near_end = false;
		 sfx[nStream].in_hnd_loop = false; // Reset loop state
		 
		 // Trace: which SFX is requesting a channel (correlate with "Reusing channel X for SFX slot Y")
		 debugf("getOpenSfxChannel: requesting for SFX \"%s\" (nStream=%d) loop=%d\n", sfx[nStream].fname, nStream + AUDIO_ENGINE_MAX_STREAMS, (int)sfx[nStream].loop);
		 // Get a new channel
		 sfx_channel = getOpenSfxChannel();
		 if(sfx_channel < 0) {
			 debugf("AudioEngine_Play: No available SFX channels!\n");
			 return;
		 }
		 sfx_channels[sfx_channel].sfx_index = nStream;
		 sfx_channels[sfx_channel].last_tick = ticks;

        aica_play_chn(
            sfx_channels[sfx_channel].mapped_ch,
            sfx[nStream].total_samples,
            sfx[nStream].aica_buffer,
            sfx[nStream].type,
            sfx[nStream].vol,
            sfx[nStream].pan, // PAN
            sfx[nStream].loop ? 1 : 0,  // hardware loop when channel wants to loop (incl. loop from start)
            sfx[nStream].rate
        );    
    }
}

struct sfx_info * AudioEngine_getSfxInfo(int nStream) {
	assert(nStream < (AUDIO_ENGINE_MAX_STREAMS + AUDIO_ENGINE_MAX_SFX));

	nStream -= AUDIO_ENGINE_MAX_STREAMS;

	return &sfx[nStream];
}

struct stream_info * AudioEngine_getStreamInfo(int nStream) {
	assert(nStream < (AUDIO_ENGINE_MAX_STREAMS));

	return &streams[nStream];
}

 int AudioEngine_GetSfxChannel(int nStream) {
	 if(nStream < AUDIO_ENGINE_MAX_STREAMS)
		 return -1; // Not an SFX
	 
	 nStream -= AUDIO_ENGINE_MAX_STREAMS;
	 int sfx_channel = getSfxChannelIndex(nStream);
	 if(sfx_channel < 0)
		 return -1;
	 
	 // Return channel number, or -1 if not allocated yet (shouldn't happen, but be safe)
	 return (sfx_channels[sfx_channel].mapped_ch >= 0) ? sfx_channels[sfx_channel].mapped_ch : -1;
 }

int AudioEngine_IsStreamPlaying(int nStream) {
	if(nStream < 0 || nStream >= AUDIO_ENGINE_MAX_STREAMS)
		return 0;

	std::lock_guard<std::mutex> lk(channel_mtx);
	return streams[nStream].playing ? 1 : 0;
}

