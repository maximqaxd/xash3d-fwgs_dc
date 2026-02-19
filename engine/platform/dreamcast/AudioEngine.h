#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <dc/spu.h>
#include <kos/mutex.h>
#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_ENGINE_MAX_STREAMS 6  // Reduced to leave channels for KOS music streaming (snd_stream_alloc needs 2 channels for stereo)
#define AUDIO_ENGINE_MAX_CHANNELS (64 - (AUDIO_ENGINE_MAX_STREAMS * 2))  // Note: channels are allocated on-demand, this is just a limit  
#define AUDIO_ENGINE_MAX_SFX 512

#define STREAM_STAGING_BUFFER_SIZE 8192
#define STREAM_STAGING_READ_SIZE_STEREO 8192
#define STREAM_STAGING_READ_SIZE_MONO (STREAM_STAGING_READ_SIZE_STEREO / 2)
#define STREAM_CHANNEL_BUFFER_SIZE (STREAM_STAGING_READ_SIZE_MONO * 2)	// lower and upper halves
#define STREAM_CHANNEL_SAMPLE_COUNT (STREAM_CHANNEL_BUFFER_SIZE * 2)		// 4 bit adpcm

/* Stub for release; */
#ifndef AUDIO_ENGINE_DEBUG
#define debugf(...)  ((void)0)
#define verbosef(...) ((void)0)
#else
#define debugf  printf
#define verbosef printf
#endif

#define MAX_WAVE_HEADER_SIZE 96

typedef struct {
    char chunkId[4];     // "RIFF"
    uint32_t chunkSize;  // File size - 8 bytes
    char format[4];      // "WAVE"
} WaveRIFFHeader;

typedef struct {
    char subchunk1Id[4];    // "fmt "
    uint32_t subchunk1Size; // Size of this subchunk (usually 16 for PCM)
    uint16_t audioFormat;   // PCM = 1
    uint16_t numChannels;   // 1 for mono, 2 for stereo, etc.
    uint32_t sampleRate;    // e.g., 44100
    uint32_t byteRate;      // SampleRate * NumChannels * BitsPerSample/8
    uint16_t blockAlign;    // NumChannels * BitsPerSample/8
    uint16_t bitsPerSample; // e.g., 16
} WaveFmtHeader;

typedef struct {
    char id[4];         // e.g., "data"
    uint32_t size;      // Size of the chunk data in bytes
} WaveChunkHeader;

struct WavHeader {
    // RIFF Header
    WaveRIFFHeader riffHeader;
	WaveFmtHeader fmtHeader;
	WaveChunkHeader chunkHeader;
};

struct stream_info {
	uint8_t buffer[STREAM_STAGING_BUFFER_SIZE + 128];
	mutex_t mtx;
	int fd;
	void *vfs_file;           // Xash3D VFS file handle (dc_file_t*) - for files in PAKs/WADs
	bool is_vfs;              // true if using VFS file handle, false if using KOS file_t
	const uint8_t* mem_data;  // For in-memory streaming
	uint32_t mem_size;         // Size of in-memory data
	uint32_t mem_offset;      // Current read offset in memory
	bool is_memory;            // true if streaming from memory, false if from file
	uint32_t aica_buffers[2]; // left, right
	int mapped_ch[2];	// left, right
	int rate;
	int total_samples;
	int played_samples;
	uint32_t file_data_offset;
	int vol;
	bool loop;
	uint32_t loop_offset;
	uint8_t pan[2];
	bool stereo;
	bool playing;
	bool next_is_upper_half;
	bool first_refill;
	bool paused;
    uint8_t type;
	uint32_t last_tick;
	char fname[128];
} __attribute__((aligned(32))); 

struct sfx_info {
    uint32_t aica_buffer; 
	uint32_t loop_offset;	
	int nSfx;
	int rate;
	int total_samples;    
	float distMin;
	float distMax;
	float fX;
	float fY;
	float fZ;
	uint8_t attenuationVol;
	uint8_t emittingVol;
	uint8_t vol;
	uint8_t pan;
	char stereo;
	char mapped_ch;
	bool loop;
	bool in_hnd_loop;
    uint8_t type;
	bool has_offset;       // true after playhead has moved past 0 (for one-shot end detection)
	bool saw_near_end;     // true after playhead reached near end (avoid false "finished" on mid-playback 0)
	uint32_t last_tick;
	char fname[128];
};

struct sfx_chnnel {
    char mapped_ch;
	uint32_t last_tick;
	int sfx_index;
};



bool AudioEngine_Initialise(void);
int AudioEngine_Load(const char * fname, uint32_t seek_bytes_aligned);
int AudioEngine_LoadFromMemory(const uint8_t * wav_data, uint32_t wav_size, const char * name);
bool AudioEngine_ParseWaveHeader_VFS(void *vfs_file, struct WavHeader *hdr); // vfs_file is dc_file_t*
#ifdef __cplusplus
// C++ version with default parameters
int AudioEngine_LoadFromWaveInfo(const uint8_t * sample_data, uint32_t sample_size, int sample_rate, int channels, int bits_per_sample, const char * name, const uint8_t * full_wav_data = nullptr, uint32_t full_wav_size = 0);
#else
// C version without default parameters
int AudioEngine_LoadFromWaveInfo(const uint8_t * sample_data, uint32_t sample_size, int sample_rate, int channels, int bits_per_sample, const char * name, const uint8_t * full_wav_data, uint32_t full_wav_size);
#endif
int AudioEngine_LoadFromVFS(void *vfs_file, const char *fname, uint32_t seek_bytes_aligned); // vfs_file is dc_file_t*
void AudioEngine_Play(int nStream, uint8_t volume, uint8_t panl, uint8_t panr, bool loop, uint32_t loop_offset);
int AudioEngine_Stop(int nStream);
int AudioEngine_Unload(int nStream);
struct sfx_info * AudioEngine_getSfxInfo(int nStream);
struct stream_info * AudioEngine_getStreamInfo(int nStream);
int AudioEngine_GetSfxChannel(int nStream); // Returns AICA channel number, or -1 if not playing
int AudioEngine_IsStreamPlaying(int nStream); // Returns 1 if stream is currently playing, 0 otherwise

#ifdef __cplusplus
}
#endif

#endif