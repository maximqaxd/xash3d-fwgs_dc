#include <kos.h>
#include <stdlib.h>
#include <string.h>

#define MPEG_MALLOC(sz)      malloc(sz)
#define MPEG_FREE(p)         free(p)
#define MPEG_REALLOC(p, sz)  realloc((p), (sz))
#define MPEG_MEMALIGN(a, sz) memalign((a), (sz))
#define MPEG_MEMZERO(p, sz)  memset(p, 0, sz)

// Use PVR allocator from ref/pvr 
#include "../../ref/pvr/pvr_alloc.h"
#define MPEG_PVR_MALLOC(sz)  alloc_malloc( NULL, sz )
#define MPEG_PVR_FREE(p)     alloc_free( NULL, p )

// Use KOS filesystem (for mpeg.c, which is from Quake2)
#define MPEG_FILE_TYPE                 file_t
#define MPEG_FILE_INVALID_HANDLE       FILEHND_INVALID
#define MPEG_FILE_OPEN(fn)             fs_open((fn), O_RDONLY)
#define MPEG_FILE_CLOSE(fh)            fs_close((fh))
#define MPEG_FILE_SEEK(fh, off, st)    fs_seek((fh), (off), (st))
#define MPEG_FILE_READ(fh, buf, size)  fs_read((fh), (buf), (size))
#define MPEG_FILE_TELL(fh)             fs_tell((fh))

#define PL_MPEG_IMPLEMENTATION
#include "mpeg.h"

struct mpeg_player_t {
    /* MPEG decoder */
    plm_t *decoder;

    /* Pointer to a decoded video frame */
    plm_frame_t *frame;

    /* Pointer to a decoded video frame */
    plm_samples_t *sample;

    /* PVR list type the video frame will be rendered to */
    pvr_list_type_t list_type;

    /* SH4 side sound buffer */
    uint8_t *snd_buf;

    /* Texture that holds decoded data */
    uint32_t texture;  // PVR texture address (use uint32_t to avoid pvr_ptr_t/pvr_txr_ptr_t compatibility issues)

    /* Width of the video in pixels */
    int width;

    /* Height of the video in pixels */
    int height;

    /* Start position for sound module playback */
    int snd_pcm_offset;

    /* Size of the sound module data */
    int snd_pcm_leftovers;

    /* Volume */
    int snd_volume;

    /* Audio sample rate */
    int sample_rate;

    /* Sound stream handle */
    snd_stream_hnd_t snd_hnd;

    /* Polygon header for rendering */
    pvr_poly_hdr_t hdr;

    /* Vertices for rendering the video frame */
    pvr_vertex_t vert[4];

    /* Start time for a/v sync */
    uint64_t start_time;
};

/* Output texture width and height initial values
   You can choose from 32, 64, 128, 256, 512, 1024 */
static int mpeg_texture_width;
static int mpeg_texture_height;

/* Size of the sound buffer for both the SH4 side and the AICA side */
#define SOUND_BUFFER (64 * 1024)

static int setup_graphics(mpeg_player_t *player, const mpeg_player_options_t *options);
static int setup_audio(mpeg_player_t *player);
static void fast_memcpy(void *dest, const void *src, size_t length);

static uint32_t next_power_of_two(uint32_t n) {
    if(n == 0)
        return 1;

    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;

    return n;
}

static inline void sound_stream_reset(mpeg_player_t *player) {
    if(!player)
        return;

    if(player->start_time != 0)
        snd_stream_stop(player->snd_hnd);

    player->snd_pcm_leftovers = 0;
    player->snd_pcm_offset = 0;
}

static int mpeg_check_cancel(const mpeg_cancel_options_t *opt) {
    if(!opt) return 0;

    /*   Controller Cancel   */
    MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
        if(opt->pad_button_any && (st->buttons & opt->pad_button_any))
            return 1;

        if(opt->pad_button_combo &&
            (st->buttons & opt->pad_button_combo) == opt->pad_button_combo)
            return 1;

        /* Always cancel on reset combo */
        if(st->buttons == CONT_RESET_BUTTONS)
            return 2;
    MAPLE_FOREACH_END()

    /*   Keyboard Cancel   */
    MAPLE_FOREACH_BEGIN(MAPLE_FUNC_KEYBOARD, kbd_state_t, kbd_st)
        for(size_t i = 0; i < opt->kbd_keys_any_count; ++i) {
            if(kbd_st->key_states[opt->kbd_keys_any[i]].is_down)
                return 1;
        }

        int all_pressed = 1;
        for(size_t i = 0; i < opt->kbd_keys_combo_count; ++i) {
            if(!kbd_st->key_states[opt->kbd_keys_combo[i]].is_down) {
                all_pressed = 0;
                break;
            }
        }

        if(opt->kbd_keys_combo_count && all_pressed)
            return 1;
    MAPLE_FOREACH_END()

    return 0;
}

/** Default MPEG player options used when NULL is passed to *_ex() functions. */
static const mpeg_player_options_t MPEG_PLAYER_OPTIONS_DEFAULT = MPEG_PLAYER_OPTIONS_INITIALIZER;

static bool mpeg_player_init(mpeg_player_t *player, const mpeg_player_options_t *opts) {
    plm_set_loop(player->decoder, opts->loop);

    player->snd_buf = (uint8_t *)MPEG_MEMALIGN(32, SOUND_BUFFER);
    if(!player->snd_buf) {
        fprintf(stderr, "Out of memory for player->snd_buf\n");
        mpeg_player_destroy(player);
        return false;
    }

    player->list_type = opts->list_type;
    player->snd_volume = opts->volume;
    if(setup_graphics(player, opts) < 0) {
        fprintf(stderr, "Setting up graphics failed\n");
        mpeg_player_destroy(player);
        return false;
    }

    if(setup_audio(player) < 0) {
        fprintf(stderr, "Setting up audio failed\n");
        mpeg_player_destroy(player);
        return false;
    }

    return true;
}

mpeg_player_t *mpeg_player_create_ex(const char *filename, const mpeg_player_options_t *options) {
    mpeg_player_t *player = NULL;
    const mpeg_player_options_t *opts = options ? options : &MPEG_PLAYER_OPTIONS_DEFAULT;

    if(!filename) {
        fprintf(stderr, "filename is NULL\n");
        return NULL;
    }

    player = (mpeg_player_t *)MPEG_MALLOC(sizeof(mpeg_player_t));
    if(!player) {
        fprintf(stderr, "Out of memory for player\n");
        return NULL;
    }

    MPEG_MEMZERO(player, sizeof(mpeg_player_t));
    player->snd_hnd = SND_STREAM_INVALID;

    player->decoder = plm_create_with_filename(filename);
    if(!player->decoder) {
        fprintf(stderr, "Out of memory for player->decoder\n");
        mpeg_player_destroy(player);
        return NULL;
    }

    if(!mpeg_player_init(player, opts))
        return NULL;

    return player;
}

mpeg_player_t *mpeg_player_create_memory_ex(unsigned char *memory, const size_t length, const mpeg_player_options_t *options) {
    mpeg_player_t *player = NULL;
    const mpeg_player_options_t *opts = options ? options : &MPEG_PLAYER_OPTIONS_DEFAULT;

    if(!memory) {
        fprintf(stderr, "memory is NULL\n");
        return NULL;
    }

    player = (mpeg_player_t *)MPEG_MALLOC(sizeof(mpeg_player_t));
    if(!player) {
        fprintf(stderr, "Out of memory for player\n");
        return NULL;
    }

    MPEG_MEMZERO(player, sizeof(mpeg_player_t));
    player->snd_hnd = SND_STREAM_INVALID;

    player->decoder = plm_create_with_memory(memory, length, 1);
    if(!player->decoder) {
        fprintf(stderr, "Out of memory for player->decoder\n");
        mpeg_player_destroy(player);
        return NULL;
    }

    if(!mpeg_player_init(player, opts))
        return NULL;

    return player;
}

mpeg_player_t *mpeg_player_create(const char *filename) {
    return mpeg_player_create_ex(filename, &MPEG_PLAYER_OPTIONS_DEFAULT);
}

mpeg_player_t *mpeg_player_create_memory(uint8_t *memory, const size_t length) {
    return mpeg_player_create_memory_ex(memory, length, &MPEG_PLAYER_OPTIONS_DEFAULT);
}

int mpeg_player_get_loop(mpeg_player_t *player) {
    if(!player)
        return -1;

    return plm_get_loop(player->decoder);
}

void mpeg_player_set_loop(mpeg_player_t *player, int loop) {
    if(!player)
        return;

    plm_set_loop(player->decoder, loop);
}

void mpeg_player_set_volume(mpeg_player_t *player, uint8_t volume) {
    if(!player)
        return;

    player->snd_volume = volume;
    snd_stream_volume(player->snd_hnd, volume);
}

void mpeg_player_destroy(mpeg_player_t *player) {
    if(!player)
        return;

    if(player->texture) {
        MPEG_PVR_FREE((void *)(uintptr_t)player->texture);
        player->texture = 0;
    }

    if(player->snd_hnd != SND_STREAM_INVALID) {
        snd_stream_destroy(player->snd_hnd);
        player->snd_hnd = SND_STREAM_INVALID;
    }

    if(player->snd_buf) {
        MPEG_FREE(player->snd_buf);
        player->snd_buf = NULL;
    }

    if(player->decoder) {
        plm_destroy(player->decoder);
        player->decoder = NULL;
    }

    MPEG_FREE(player);
    player = NULL;
}

mpeg_play_result_t mpeg_play_ex(mpeg_player_t *player, const mpeg_cancel_options_t *cancel_options) {
    mpeg_play_result_t result = MPEG_PLAY_NORMAL;

    if(!player || !player->decoder)
        return MPEG_PLAY_ERROR;

    /* Init sound stream. */
    sound_stream_reset(player);
    snd_stream_start(player->snd_hnd, player->sample_rate, 0);
    snd_stream_volume(player->snd_hnd, player->snd_volume);

    player->frame = plm_decode_video(player->decoder);
    if(!player->frame) {
        /* Reset some stuff */
        sound_stream_reset(player);
        return MPEG_PLAY_ERROR;
    }
    player->start_time = timer_ns_gettime64();

    while(true) {
        /* Get elapsed playback time */
        double playback_time = (timer_ns_gettime64() - player->start_time) * 1e-9f;

        /* Check cancel matching */
        int cancel = mpeg_check_cancel(cancel_options);
        if(cancel == 1 || cancel == 2) {
            result = (cancel == 1) ? MPEG_PLAY_CANCEL_INPUT : MPEG_PLAY_CANCEL_RESET;
            goto finish;
        }

        /* Poll audio regardless */
        snd_stream_poll(player->snd_hnd);

        if(playback_time >= player->frame->time) {
            /* Render the current frame */
            pvr_wait_ready();
            pvr_scene_begin();
            mpeg_upload_frame(player);

            pvr_list_begin(player->list_type);

            mpeg_draw_frame(player);

            pvr_list_finish();
            pvr_scene_finish();

            /* Decode the NEXT frame to have it ready */
            //uint64_t startd_time = timer_ns_gettime64();
            player->frame = plm_decode_video(player->decoder);
            //uint64_t decode_time = timer_ns_gettime64() - startd_time;
            //printf("%" PRIu64 "\n", decode_time);

            if(!player->frame) {
                /* Are we looping? */
                if(!plm_get_loop(player->decoder)) {
                    result = MPEG_PLAY_NORMAL;
                    goto finish;
                }

                /* We are looping. Reset and restart */
                sound_stream_reset(player);
                snd_stream_start(player->snd_hnd, player->sample_rate, 0);
                snd_stream_volume(player->snd_hnd, player->snd_volume);

                player->frame = plm_decode_video(player->decoder);
                if(!player->frame) {
                    result = MPEG_PLAY_ERROR;
                    goto finish;
                }

                player->start_time = timer_ns_gettime64();
            }
        }
    }

finish:
    /* Reset some stuff */
    sound_stream_reset(player);
    player->start_time = 0;

    return result;
}

mpeg_play_result_t mpeg_play(mpeg_player_t *player, uint32_t cancel_buttons) {
    mpeg_cancel_options_t opts = {
        .pad_button_any = cancel_buttons
    };

    return mpeg_play_ex(player, &opts);
}

mpeg_decode_result_t mpeg_decode_step(mpeg_player_t *player) {
    if(!player || !player->decoder)
        return MPEG_DECODE_ERROR;

    if(player->start_time == 0) {
        /* Init sound stream. */
        sound_stream_reset(player);
        snd_stream_start(player->snd_hnd, player->sample_rate, 0);
        snd_stream_volume(player->snd_hnd, player->snd_volume);

        /* Prime the first frame */
        player->frame = plm_decode_video(player->decoder);
        if(!player->frame)
            return MPEG_DECODE_EOF;

        player->start_time = timer_ns_gettime64();

        /* Poll first thing as well since we have a video frame ready */
        snd_stream_poll(player->snd_hnd);
        return MPEG_DECODE_FRAME;
    }

    /* Get elapsed playback time */
    double playback_time = (timer_ns_gettime64() - player->start_time) * 1e-9f;

    /* Poll audio regardless */
    snd_stream_poll(player->snd_hnd);

    /* Check if it's time to decode the next frame */
    if(playback_time >= player->frame->time) {
        player->frame = plm_decode_video(player->decoder);
        if(player->frame)
            return MPEG_DECODE_FRAME;

        /* Are we looping? */
        if(!plm_get_loop(player->decoder)) {
            sound_stream_reset(player);
            return MPEG_DECODE_EOF;
        }

        /* We are Looping. Reset and restart */
        sound_stream_reset(player);
        snd_stream_start(player->snd_hnd, player->sample_rate, 0);
        snd_stream_volume(player->snd_hnd, player->snd_volume);

        player->frame = plm_decode_video(player->decoder);
        if(!player->frame) {
            sound_stream_reset(player);
            return MPEG_DECODE_EOF;
        }

        player->start_time = timer_ns_gettime64();
        return MPEG_DECODE_FRAME;
    }

    return MPEG_DECODE_IDLE;
}

void mpeg_upload_frame(mpeg_player_t *player) {
    if(!player || !player->frame)
        return;

    /* HACK: Fix Flycast */
    PVR_SET(PVR_YUV_CFG, (((mpeg_texture_height / 16) - 1) << 8) |
                      ((mpeg_texture_width / 16) - 1));

    uint32_t *src = player->frame->display;

    /* Video size in macroblocks (16x16) */
    const int video_blocks_w = player->frame->width  >> 4;
    const int video_blocks_h = player->frame->height >> 4;

    /*
     * PVR YUV converter stride (in macroblocks).
     * This MUST match the width configured in PVR_YUV_CFG.
     *
     * Example:
     *   mpeg_texture_width = 512 px
     *   → stride = 512 / 16 = 32 macroblocks
     */
    const int pvr_blocks_per_row = mpeg_texture_width >> 4;
    const int pad_blocks_x = pvr_blocks_per_row - video_blocks_w;

    /*
     * Each macroblock is 384 bytes = 96 uint32_t
     * sq_fast_cpy works in 32-byte chunks → 384 / 32 = 12 iterations
     */
    const int mb_sq_iters = 384 / 32;

    uint32_t *d = SQ_MASK_DEST((void *)PVR_TA_YUV_CONV);
    sq_lock((void *)PVR_TA_YUV_CONV);

    for(int y = 0; y < video_blocks_h; y++) {
        /* Upload real macroblocks */
        for(int x = 0; x < video_blocks_w; x++, src += 96) {
            sq_fast_cpy(d, src, mb_sq_iters);
        }

        /* Pad row to PVR stride */
        for(int i = 0; i < pad_blocks_x * mb_sq_iters; i++) {
            sq_flush(d);
        }
    }

    sq_unlock();
}

void mpeg_draw_frame(mpeg_player_t *player) {
    if(!player || !player->frame)
        return;

    pvr_prim(&player->hdr, sizeof(pvr_poly_hdr_t));

    pvr_prim(&player->vert[0], sizeof(pvr_vertex_t));
    pvr_prim(&player->vert[1], sizeof(pvr_vertex_t));
    pvr_prim(&player->vert[2], sizeof(pvr_vertex_t));
    pvr_prim(&player->vert[3], sizeof(pvr_vertex_t));
}

static int setup_graphics(mpeg_player_t *player, const mpeg_player_options_t *opts) {
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    float screen_width = (float)vid_mode->width;
    float screen_height = (float)vid_mode->height;
    player->width = plm_get_width(player->decoder);
    player->height = plm_get_height(player->decoder);

    /* Check if the w/h ratio matches the screen */
    float video_ratio = (float)player->width / (float)player->height;
    float screen_ratio = screen_width / screen_height;

    /* If the video ratio is not one that will fit the screen nicely when stretched */
    if(fabsf(video_ratio - screen_ratio) > 0.0001f) {
        if(video_ratio > screen_ratio) {
            /* Video is wider than screen, adjust height */
            screen_height = screen_width / video_ratio;
            screen_y = ((float)vid_mode->height - screen_height) / 2.0f;
        } else {
            /* Video is taller than screen, adjust width */
            screen_width = screen_height * video_ratio;
            screen_x = ((float)vid_mode->width - screen_width) / 2.0f;
        }
    }

    mpeg_texture_width = next_power_of_two(player->width);
    mpeg_texture_height = next_power_of_two(player->height);

    void *texture_ptr = MPEG_PVR_MALLOC(mpeg_texture_width * mpeg_texture_height * 2);
    if(!texture_ptr) {
        fprintf(stderr, "Failed to allocate PVR memory! (requested %d bytes, texture %dx%d)\n", 
                mpeg_texture_width * mpeg_texture_height * 2, mpeg_texture_width, mpeg_texture_height);
        // Check if allocator is initialized
        void *base = alloc_base_address(NULL);
        if(!base) {
            fprintf(stderr, "PVR allocator not initialized!\n");
        }
        return -1;
    }
    player->texture = (uint32_t)(uintptr_t)texture_ptr;

    /* Set SQ to YUV converter. */
    PVR_SET(PVR_YUV_ADDR, (player->texture & 0xffffff));
    /* Divide texture width and texture height by 16 and subtract 1.
       The actual values to set are 1, 3, 7, 15, 31, 63. */
    PVR_SET(PVR_YUV_CFG, (((mpeg_texture_height / 16) - 1) << 8) |
                          ((mpeg_texture_width / 16) - 1));
    PVR_GET(PVR_YUV_CFG);

    /* Clear texture to black */
    sq_set((void *)(uintptr_t)player->texture, 0, mpeg_texture_width * mpeg_texture_height * 2);

    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt, player->list_type,
                     PVR_TXRFMT_YUV422 | PVR_TXRFMT_NONTWIDDLED,
                     mpeg_texture_width, mpeg_texture_height,
                     (pvr_txr_ptr_t)player->texture,
                     opts->filter_mode);
    pvr_poly_compile(&player->hdr, &cxt);

    float u = (float)player->width / mpeg_texture_width;
    float v = (float)player->height / mpeg_texture_height;
    float left   = screen_x;
    float top    = screen_y;
    float right  = screen_x + screen_width;
    float bottom = screen_y + screen_height;
    int color = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);

    player->vert[0].x = left;
    player->vert[0].y = top;
    player->vert[0].z = 1.0f;
    player->vert[0].u = 0.0f;
    player->vert[0].v = 0.0f;
    player->vert[0].argb = color;
    player->vert[0].oargb = 0;
    player->vert[0].flags = PVR_CMD_VERTEX;

    player->vert[1].x = right;
    player->vert[1].y = top;
    player->vert[1].z = 1.0f;
    player->vert[1].u = u;
    player->vert[1].v = 0.0f;
    player->vert[1].argb = color;
    player->vert[1].oargb = 0;
    player->vert[1].flags = PVR_CMD_VERTEX;

    player->vert[2].x = left;
    player->vert[2].y = bottom;
    player->vert[2].z = 1.0f;
    player->vert[2].u = 0.0f;
    player->vert[2].v = v;
    player->vert[2].argb = color;
    player->vert[2].oargb = 0;
    player->vert[2].flags = PVR_CMD_VERTEX;

    player->vert[3].x = right;
    player->vert[3].y = bottom;
    player->vert[3].z = 1.0f;
    player->vert[3].u = u;
    player->vert[3].v = v;
    player->vert[3].argb = color;
    player->vert[3].oargb = 0;
    player->vert[3].flags = PVR_CMD_VERTEX_EOL;

    return 0;
}

static void *sound_callback(snd_stream_hnd_t hnd, int request_size, int *size_out) {
    mpeg_player_t *player = (mpeg_player_t *)snd_stream_get_userdata(hnd);
    const int frame_bytes = PLM_AUDIO_SAMPLES_PER_FRAME * (int)sizeof(short);
    uint8_t *dest = player->snd_buf;
    int out = 0;
    int needed = request_size;

    while(needed > 0) {
        if(player->snd_pcm_leftovers > 0 && player->sample) {
            int chunk = player->snd_pcm_leftovers;
            if(chunk > needed)
                chunk = needed;

            fast_memcpy(dest + out, (uint8_t *)player->sample->pcm + player->snd_pcm_offset, chunk);
            out += chunk;
            needed -= chunk;
            player->snd_pcm_offset += chunk;
            player->snd_pcm_leftovers -= chunk;
            continue;
        }

        player->sample = plm_decode_audio(player->decoder);
        if(!player->sample)
            break;

        player->snd_pcm_offset = 0;
        player->snd_pcm_leftovers = frame_bytes;
    }

    if(needed > 0) {
        MPEG_MEMZERO(dest + out, needed);
        out += needed;
    }

    *size_out = request_size;

    return player->snd_buf;
}

static int setup_audio(mpeg_player_t *player) {
    player->snd_pcm_leftovers = 0;
    player->snd_pcm_offset = 0;
    player->sample_rate = plm_get_samplerate(player->decoder);

    player->snd_hnd = snd_stream_alloc(sound_callback, SOUND_BUFFER);
    if(player->snd_hnd == SND_STREAM_INVALID)
        return -1;

    snd_stream_set_userdata(player->snd_hnd, player);

    return 0;
}

// Accessor function for Xash3D to get decoder
plm_t *mpeg_player_get_decoder(mpeg_player_t *player) {
	if(!player)
		return NULL;
	return player->decoder;
}

// Wrapper functions for frame-by-frame playback
int mpeg_player_seek(mpeg_player_t *player, double time, int seek_exact) {
	if(!player || !player->decoder)
		return 0;
	return plm_seek(player->decoder, time, seek_exact);
}

plm_frame_t *mpeg_player_decode_video(mpeg_player_t *player) {
	if(!player || !player->decoder)
		return NULL;
	return plm_decode_video(player->decoder);
}

void mpeg_player_set_frame(mpeg_player_t *player, plm_frame_t *frame) {
	if(!player)
		return;
	player->frame = frame;
}

plm_frame_t *mpeg_player_get_frame(mpeg_player_t *player) {
	if(!player)
		return NULL;
	return player->frame;
}

// Wrapper functions for video/audio info
int mpeg_player_get_width(mpeg_player_t *player) {
	if(!player || !player->decoder)
		return 0;
	return plm_get_width(player->decoder);
}

int mpeg_player_get_height(mpeg_player_t *player) {
	if(!player || !player->decoder)
		return 0;
	return plm_get_height(player->decoder);
}

double mpeg_player_get_framerate(mpeg_player_t *player) {
	if(!player || !player->decoder)
		return 0.0;
	return plm_get_framerate(player->decoder);
}

double mpeg_player_get_duration(mpeg_player_t *player) {
	if(!player || !player->decoder)
		return 0.0;
	return plm_get_duration(player->decoder);
}

int mpeg_player_get_num_audio_streams(mpeg_player_t *player) {
	if(!player || !player->decoder)
		return 0;
	return plm_get_num_audio_streams(player->decoder);
}

int mpeg_player_get_samplerate(mpeg_player_t *player) {
	if(!player || !player->decoder)
		return 0;
	return plm_get_samplerate(player->decoder);
}

int mpeg_player_start_audio(mpeg_player_t *player) {
	if(!player || player->snd_hnd == SND_STREAM_INVALID)
		return 0;
	sound_stream_reset(player);
	snd_stream_start(player->snd_hnd, player->sample_rate, 0);
	snd_stream_volume(player->snd_hnd, player->snd_volume);
	return 1;
}

void mpeg_player_poll_audio(mpeg_player_t *player) {
	if(!player || player->snd_hnd == SND_STREAM_INVALID)
		return;
	snd_stream_poll(player->snd_hnd);
}

void mpeg_player_stop_audio(mpeg_player_t *player) {
	if(!player || player->snd_hnd == SND_STREAM_INVALID)
		return;
	sound_stream_reset(player);
}

static __attribute__((noinline)) void fast_memcpy(void *dest, const void *src, size_t length) {
    uintptr_t dest_ptr = (uintptr_t)dest;
    uintptr_t src_ptr = (uintptr_t)src;

    _Complex float ds, ds2, ds3, ds4;

    if(((dest_ptr | src_ptr) & 7) || length < 32) {
        memcpy(dest, src, length);
    }
    else { /* Fast Path */
        int blocks = (int)(length >> 5);
        int remainder = (int)(length & 31);

        if(blocks > 0) {
            __asm__ __volatile__ (
                "fschg\n\t"
                ".align 2\n"
                "1:\n\t"
                /* *dest++ = *src++ */
                "fmov.d @%[in]+, %[scratch]\n\t"
                "fmov.d @%[in]+, %[scratch2]\n\t"
                "fmov.d @%[in]+, %[scratch3]\n\t"
                "fmov.d @%[in]+, %[scratch4]\n\t"
                "movca.l %[r0], @%[out]\n\t"
                "add #32, %[out]\n\t"
                "dt %[blocks]\n\t"   /* while(blocks--) */
                "fmov.d %[scratch4], @-%[out]\n\t"
                "fmov.d %[scratch3], @-%[out]\n\t"
                "fmov.d %[scratch2], @-%[out]\n\t"
                "fmov.d %[scratch], @-%[out]\n\t"
                "bf.s 1b\n\t"
                "add #32, %[out]\n\t"
                "fschg\n"
                : [in] "+&r" (src_ptr),
                  [out] "+&r" (dest_ptr),
                  [blocks] "+&r" (blocks),
                  [scratch] "=&d" (ds),
                  [scratch2] "=&d" (ds2),
                  [scratch3] "=&d" (ds3),
                  [scratch4] "=&d" (ds4) /* outputs */
                : [r0] "z" (0) /* inputs */
                : "t", "memory" /* clobbers */
            );
        }

        char *char_dest = (char *)dest_ptr;
        const char *char_src = (const char *)src_ptr;

        while(remainder--)
            *char_dest++ = *char_src++;
    }
}
