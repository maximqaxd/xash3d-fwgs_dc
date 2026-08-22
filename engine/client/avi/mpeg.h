/**
    MPEG1 Decode Library for Dreamcast - Version 0.8 (2023/09/19)
    Originally ported by Tashi (aka Twada)
    Further edits done by Ian Robinson and Andy Barajas

    Overview:
    This library facilitates the playback of MPEG1 videos on the Sega Dreamcast console.
    It supports monaural audio and allows specifying a cancel button during playback.

    Key Features:
    - Video Playback: MPEG1 video playback.
    - Audio Support: Mono audio playback. Stereo videos will play only the left channel.
    - Cancel Button: Allows specifying a controller button combination to cancel playback.
    - Recommended Resolutions:
      - 4:3 Aspect Ratio: 320x240 pixels, Mono audio at 80kbits.
      - 16:9 Aspect Ratio: 368x208 pixels, Mono audio at 80kbits.

    To create compatible MPEG1 videos, use the following ffmpeg command:

    ffmpeg -i input.mp4 -vf "scale=320:240" -b:v 742k -minrate 742k -maxrate 742k -bufsize 742k -ac 1 -ar 32000 -c:a mp2 -b:a 64k -f mpeg output.mpg
 */

#ifndef _MPEG_H_INCLUDED_
#define _MPEG_H_INCLUDED_

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration for plm_t (defined in pl_mpeg.h)
struct plm_t;
typedef struct plm_t plm_t;

#include <stdio.h>

// Include pvr_mem.h before pvr_header.h to define pvr_ptr_t
#include <dc/pvr/pvr_mem.h>
#include <dc/pvr/pvr_header.h>

/**
    \defgroup mpeg_customization Build-Time Customization
    \ingroup mpeg_playback

    The MPEG playback library supports compile-time customization of memory allocation,
    video RAM usage, and file I/O. This is done via a set of macros that must be defined
    **before including `mpeg.h`**.

    ---
    ## Memory Allocation (`MPEG_*` macros)

    These control how memory is allocated on the SH-4 side for the MPEG player and decoder.

    If you do not define them, the following defaults are used:

    ```c
    #define MPEG_MALLOC(sz)        malloc(sz)
    #define MPEG_FREE(p)           free(p)
    #define MPEG_REALLOC(p, sz)    realloc((p), (sz))
    #define MPEG_MEMALIGN(a, sz)   memalign((a), (sz))
    #define MPEG_MEMZERO(p, sz)    memset((p), 0, (sz))
    ```

    If you override **any** of these, you **must override all five**.

    ---
    ## PVR (Video RAM) Allocation (`MPEG_PVR_*` macros)

    These control how textures (video frames) are allocated in VRAM.

    Defaults:

    ```c
    #define MPEG_PVR_MALLOC(sz)    pvr_mem_malloc(sz)
    #define MPEG_PVR_FREE(p)       pvr_mem_free(p)
    ```

    If you override one, you must override both.

    ---
    ## File I/O (`MPEG_FILE_*` macros)

    These define how the MPEG decoder opens and reads MPEG files.

    If none are defined, the library uses KallistiOS file I/O:

    ```c
    #define MPEG_FILE_TYPE                 file_t
    #define MPEG_FILE_INVALID_HANDLE       FILEHND_INVALID
    #define MPEG_FILE_OPEN(fn)             fs_open((fn), O_RDONLY)
    #define MPEG_FILE_CLOSE(fh)            fs_close((fh))
    #define MPEG_FILE_SEEK(fh, off, st)    fs_seek((fh), (off), (st))
    #define MPEG_FILE_READ(fh, buf, size)  fs_read((fh), (buf), (size))
    #define MPEG_FILE_TELL(fh)             fs_tell((fh))
    ```

    If you override **any one** of these macros, you must override **all seven** to ensure compatibility and prevent undefined behavior.

    ---
    ## Compile-time Enforcement

    If you attempt a partial override (e.g. redefine only one macro from a group), compilation will fail with an error message. This ensures:

    - Proper pairing of allocation and free functions
    - Consistent file handle semantics
    - Prevention of subtle memory or I/O bugs

    Always define the **entire macro group** when customizing behavior.
*/

/* --- Compile-time check for consistent macro overrides --- */
#if defined(MPEG_MALLOC) || defined(MPEG_FREE) || defined(MPEG_REALLOC) || \
    defined(MPEG_MEMALIGN) || defined(MPEG_MEMZERO)
    #if !defined(MPEG_MALLOC) || !defined(MPEG_FREE) || !defined(MPEG_REALLOC) || \
        !defined(MPEG_MEMALIGN) || !defined(MPEG_MEMZERO)
        #error "If you override any MPEG memory macros (MPEG_MALLOC, MPEG_FREE, etc), you must override ALL of them."
    #endif
#else
	#define MPEG_MALLOC(sz)      malloc(sz)
	#define MPEG_FREE(p)         free(p)
	#define MPEG_REALLOC(p, sz)  realloc((p), (sz))
    #define MPEG_MEMALIGN(a, sz) memalign((a), (sz))
    #define MPEG_MEMZERO(p, sz)  memset(p, 0, sz)
#endif

#if defined(MPEG_PVR_MALLOC) || defined(MPEG_PVR_FREE)
    #if !defined(MPEG_PVR_MALLOC) || !defined(MPEG_PVR_FREE)
        #error "If you override MPEG_PVR_MALLOC or MPEG_PVR_FREE, you must override BOTH."
    #endif
#else
	#define MPEG_PVR_MALLOC(sz)  pvr_mem_malloc(sz)
	#define MPEG_PVR_FREE(p)     pvr_mem_free(p)
#endif

#if defined(MPEG_FILE_TYPE) || defined(MPEG_FILE_INVALID_HANDLE) || \
    defined(MPEG_FILE_OPEN) || defined(MPEG_FILE_CLOSE)          || \
    defined(MPEG_FILE_SEEK) || defined(MPEG_FILE_READ)           || \
    defined(MPEG_FILE_TELL)

    #if !defined(MPEG_FILE_TYPE) || !defined(MPEG_FILE_INVALID_HANDLE) || \
        !defined(MPEG_FILE_OPEN) || !defined(MPEG_FILE_CLOSE)          || \
        !defined(MPEG_FILE_SEEK) || !defined(MPEG_FILE_READ)           || \
        !defined(MPEG_FILE_TELL)
        #error "If you override any MPEG_FILE_* macro, you must override all: TYPE, INVALID_HANDLE, OPEN, CLOSE, SEEK, READ, TELL."
    #endif
#else
    #define MPEG_FILE_TYPE                 file_t
    #define MPEG_FILE_INVALID_HANDLE       FILEHND_INVALID
    #define MPEG_FILE_OPEN(fn)             fs_open((fn), O_RDONLY)
    #define MPEG_FILE_CLOSE(fh)            fs_close((fh))
    #define MPEG_FILE_SEEK(fh, off, st)    fs_seek((fh), (off), (st))
    #define MPEG_FILE_READ(fh, buf, size)  fs_read((fh), (buf), (size))
    #define MPEG_FILE_TELL(fh)             fs_tell((fh))
#endif

// Include pl_mpeg.h (without implementation) to get plm_frame_t type definition
// This is needed because mpeg.h declares functions that return plm_frame_t*
// The include guard in pl_mpeg.h will prevent multiple inclusions, but that's OK
// because mpeg.c will define PL_MPEG_IMPLEMENTATION before including pl_mpeg.h
// for the first time (through mpeg.h)
#include "pl_mpeg.h"

typedef struct mpeg_player_t mpeg_player_t;

/** \brief   Create an MPEG player instance.
    \ingroup mpeg_playback

    This function initializes an MPEG player for video and audio playback.
    It allocates memory for the mpeg_player_t structure, initializes the MPEG
    decoder with the given filename, and sets up the graphics and audio systems
    for playback.

    \param  filename        The filename of the MPEG file to be played. Must not be NULL.
    \return                 A pointer to an initialized mpeg_player_t structure,
                            or NULL if initialization fails at any stage.
*/
mpeg_player_t *mpeg_player_create(const char *filename);

/** \brief   Create an MPEG player instance from memory.
    \ingroup mpeg_playback

    This function initializes an MPEG player for video and audio playback
    using MPEG data stored in memory. It allocates memory for the mpeg_player_t
    structure, initializes the MPEG decoder with the provided memory buffer,
    and sets up the graphics and audio systems for playback.

    \param  memory          The pointer to the MPEG data in memory. Must not be NULL.
    \param  length          The size of the MPEG data in bytes.
    \return                 A pointer to an initialized mpeg_player_t structure,
                            or NULL if initialization fails at any stage.
*/
mpeg_player_t *mpeg_player_create_memory(unsigned char *memory, const size_t length);

/**
 * \struct mpeg_player_options_t
 * Playback options for MPEG player.
 */
typedef struct mpeg_player_options_t {
    pvr_list_type_t     list_type;    /**< PVR polygon list type */
    pvr_filter_mode_t   filter_mode;  /**< Texture filter mode */
    uint8_t             volume;       /**< Volume (0–255) */
    bool                loop;         /**< Enable looping */
} mpeg_player_options_t;

/**
 * \def MPEG_PLAYER_OPTIONS_INITIALIZER
 * Default initializer for `mpeg_player_options_t`.
 *
 * Defaults:
 * - `list_type`   = `PVR_LIST_OP_POLY`
 * - `filter_mode` = `PVR_FILTER_BILINEAR`
 * - `volume`      = `255`
 * - `loop`        = `false`
 *
 * Example:
 * ```c
 * mpeg_player_options_t opts = MPEG_PLAYER_OPTIONS_INITIALIZER;
 * opts.loop = true;
 * ```
 */
#define MPEG_PLAYER_OPTIONS_INITIALIZER \
    { PVR_LIST_OP_POLY, PVR_FILTER_BILINEAR, 255, false }

/** \brief   Create an MPEG player instance with custom options.
    \ingroup mpeg_playback

    This function initializes an MPEG player for video and audio playback using
    the specified filename and a set of player options. It behaves like
    `mpeg_player_create()` but allows customization of playback parameters such
    as volume, looping behavior, and PVR rendering options.

    If the \p options parameter is NULL, default player settings are used.

    \param  filename        The filename of the MPEG file to be played.
                            Must not be NULL.
    \param  options         Optional pointer to a mpeg_player_options_t structure
                            specifying playback and rendering options.
                            May be NULL to use defaults.
    \return                 A pointer to an initialized mpeg_player_t structure,
                            or NULL if initialization fails at any stage.
*/
mpeg_player_t *mpeg_player_create_ex(const char *filename, const mpeg_player_options_t *options);

/** \brief   Create an MPEG player instance from memory with custom options.
    \ingroup mpeg_playback

    This function initializes an MPEG player for video and audio playback using
    MPEG data stored entirely in memory and a set of player options. It behaves
    like `mpeg_player_create_memory()` but allows customization of playback
    parameters such as volume, looping behavior, and PVR rendering options.

    If the \p options parameter is NULL, default player settings are used.

    \param  memory          Pointer to the MPEG data in memory.
                            Must not be NULL.
    \param  length          Size of the MPEG data in bytes.
    \param  options         Optional pointer to a mpeg_player_options_t structure
                            specifying playback and rendering options.
                            May be NULL to use defaults.
    \return                 A pointer to an initialized mpeg_player_t structure,
                            or NULL if initialization fails at any stage.
*/
mpeg_player_t *mpeg_player_create_memory_ex(unsigned char *memory, const size_t length, const mpeg_player_options_t *options);

/**
    \brief   Retrieves the loop status of the MPEG player.
    \ingroup mpeg_playback

    This function checks whether the MPEG player is set to loop playback.

    \param   player  The MPEG player instance.
    \return          An integer representing the loop status (non-zero for loop).
 */
int mpeg_player_get_loop(mpeg_player_t *player);

/**
    \brief   Sets the loop status of the MPEG player.
    \ingroup mpeg_playback

    This function configures the MPEG player to either loop or not loop playback
    based on the provided loop parameter.

    \param   player  The MPEG player instance to configure.
    \param   loop    An integer indicating the desired loop status (non-zero for loop).
 */
void mpeg_player_set_loop(mpeg_player_t *player, int loop);

/**
    \brief   Sets the volume of the MPEG player's audio output.
    \ingroup mpeg_playback

    This function adjusts the playback volume for the MPEG player's audio stream.
    The volume value should be in the range 0 (mute) to 255 (maximum volume).

    \param   player  The MPEG player instance to configure.
    \param   volume  An unsigned 8-bit integer specifying the desired volume level.
                     A value of 0 mutes the audio; 255 is the maximum volume.
 */
void mpeg_player_set_volume(mpeg_player_t *player, uint8_t volume);

/** \brief   Destroy an MPEG player instance.
    \ingroup mpeg_playback

    This function releases all resources associated with an MPEG player. It
    frees the memory allocated for the mpeg_player_t structure and any internal
    components such as the MPEG decoder, texture, and sound buffer. If a valid
    sound handle exists, it is also destroyed.

    \param  player          The pointer to the mpeg_player_t structure to be destroyed.
                            If NULL, the function does nothing.
*/
void mpeg_player_destroy(mpeg_player_t *player);

/**
    \brief   Return codes for MPEG playback result.
    \ingroup mpeg_playback
*/
typedef enum {
    MPEG_PLAY_ERROR         = -1, /**< The player or decoder was NULL */
    MPEG_PLAY_NORMAL        =  0, /**< Playback finished normally */
    MPEG_PLAY_CANCEL_INPUT  =  1, /**< Cancelled via controller or keyboard input */
    MPEG_PLAY_CANCEL_RESET  =  2  /**< Cancelled via ABXY+START reset combo */
} mpeg_play_result_t;

/** \brief   Play an MPEG video using an MPEG player.
    \ingroup mpeg_playback

    This function starts the playback of an MPEG video using the specified
    MPEG player instance. It continuously decodes video frames and handles
    audio streaming while checking for cancellation inputs via controller buttons.

    \param  player          The MPEG player instance used for playback. Must be initialized.
    \param  cancel_buttons  A bit mask of controller buttons that can cancel the playback.
    \return                 A value from \ref mpeg_play_result_t indicating the result:
                            - `MPEG_PLAY_NORMAL` (0):
                                Playback finished normally.
                            - `MPEG_PLAY_CANCEL_INPUT` (1):
                                Playback was cancelled via controller or keyboard input.
                            - `MPEG_PLAY_CANCEL_RESET` (2):
                                Playback was cancelled via the reset combo (ABXY + START).
                            - `MPEG_PLAY_ERROR` (-1):
                                The player or decoder was NULL.
*/
mpeg_play_result_t mpeg_play(mpeg_player_t *player, uint32_t cancel_buttons);

/** \brief   Input cancellation options for MPEG playback.
    \ingroup mpeg_playback

    This structure defines user input combinations that can cancel MPEG video playback
    when passed to `mpeg_play_ex()`.

    It supports cancel detection via:
    - Controller buttons (any or combo)
    - Keyboard keys (any or combo)

    Each group is optional — set unused fields to 0 or NULL. If both controller and
    keyboard cancel checks are defined, either can trigger cancellation.

    Use this to implement fine-grained control over when playback should exit,
    such as when the Escape key is pressed, or a button combo like L+R+START.

    Example usage:
    \code
    const uint16_t keys[] = { KBD_KEY_ESCAPE };
    mpeg_cancel_options_t opts = {
        .pad_button_any = CONT_START,
        .kbd_keys_any = keys,
        .kbd_keys_any_count = 1
    };
    mpeg_play_ex(player, &opts);
    \endcode
*/
typedef struct mpeg_cancel_options_t {
    /* Pad */
    uint32_t pad_button_any;    /* Any of these triggers cancel */
    uint32_t pad_button_combo;  /* All of these must be held to cancel */

    /* Keyboard */
    const uint16_t *kbd_keys_any;   /* Array of keys - cancel if any pressed */
    size_t kbd_keys_any_count;

    const uint16_t *kbd_keys_combo; /* Array of keys - Cancel only if all of these keys are pressed */
    size_t kbd_keys_combo_count;
} mpeg_cancel_options_t;

/** \brief   Play an MPEG video with extended input cancel options.
    \ingroup mpeg_playback

    This function starts playback of an MPEG video using the specified MPEG player
    instance. It continuously decodes video frames, renders them, and streams audio
    while checking for input-based cancellation. Unlike the simpler mpeg_play() variant,
    this function allows for more granular cancellation input through controller button
    masks (any or combo) and keyboard key matching (any or combo).

    Use this function if you need fine-grained control over what input combinations
    cancel video playback (e.g., keyboard escape key, button combos, or reset patterns).

    \param  player          The MPEG player instance used for playback. Must be initialized.
    \param  cancel_options  A pointer to a mpeg_cancel_options_t struct describing
                            which controller and/or keyboard inputs should cancel playback.
                            May be NULL to disable cancel checks.
    \return                 A value from \ref mpeg_play_result_t indicating the result:
                            - `MPEG_PLAY_NORMAL` (0):
                                Playback finished normally.
                            - `MPEG_PLAY_CANCEL_INPUT` (1):
                                Playback was cancelled via controller or keyboard input.
                            - `MPEG_PLAY_CANCEL_RESET` (2):
                                Playback was cancelled via the reset combo (ABXY + START).
                            - `MPEG_PLAY_ERROR` (-1):
                                The player or decoder was NULL.
*/
mpeg_play_result_t mpeg_play_ex(mpeg_player_t *player, const mpeg_cancel_options_t *cancel_options);

/**
    \brief   Return codes for MPEG decode operations.
    \ingroup mpeg_playback
*/
typedef enum {
    MPEG_DECODE_ERROR    = -1, /**< Invalid input or decoder error */
    MPEG_DECODE_EOF      = -2, /**< Reached end of stream and not looping */
    MPEG_DECODE_IDLE     =  0, /**< No frame decoded (waiting on audio) */
    MPEG_DECODE_FRAME    =  1  /**< Frame successfully decoded */
} mpeg_decode_result_t;

/** \brief   Decode the next video frame step (non-blocking).
    \ingroup mpeg_playback

    This function performs a single decoding step for the MPEG player. It checks
    whether it's time to decode a new video frame based on synchronization with
    the audio playback time. If decoding is required, it attempts to decode the
    next frame from the video stream and updates the internal timing.

    This function is useful for use in a game loop or custom playback control logic.

    \param  player      The MPEG player instance. Must be initialized.
    \return             A value from \ref mpeg_decode_result_t indicating the result:
                        - `MPEG_DECODE_FRAME`:
                            A video frame was successfully decoded.
                        - `MPEG_DECODE_IDLE`:
                            No frame was decoded (e.g., waiting for audio to catch up).
                        - `MPEG_DECODE_EOF`:
                            End of stream reached and looping is disabled.
                        - `MPEG_DECODE_ERROR`:
                            The player or decoder is NULL.
 */
mpeg_decode_result_t mpeg_decode_step(mpeg_player_t *player);

/** \brief   Upload the most recently decoded video frame to PVR YUV converter memory.
    \ingroup mpeg_playback

    This function transfers the latest decoded frame from the MPEG decoder's internal
    buffer into the PVR YUV converter memory using DMA-friendly store queues.

    The frame must have already been decoded using `mpeg_decode_step()` or
    through the playback loop.

    \param  player      The MPEG player instance. Must be initialized and must
                        have a valid frame decoded.
 */
void mpeg_upload_frame(mpeg_player_t *player);

/** \brief   Render the most recently uploaded frame to the screen.
    \ingroup mpeg_playback

    This function draws the currently uploaded MPEG frame using the Dreamcast's PVR
    rendering system. It assumes that `mpeg_upload_frame()` has already been called
    for the current frame and that a PVR scene is active.

    The function submits a single textured quad using the PVR YUV texture and
    compiled polygon header.

    \param  player      The MPEG player instance. Must be initialized.
 */
void mpeg_draw_frame(mpeg_player_t *player);

/** \brief   Get the underlying PL_MPEG decoder from an MPEG player.
    \ingroup mpeg_playback

    This function returns a pointer to the internal PL_MPEG decoder instance
    used by the MPEG player. This allows direct access to PL_MPEG functions
    for advanced use cases such as frame-by-frame seeking.

    \param  player      The MPEG player instance.
    \return             A pointer to the plm_t decoder, or NULL if player is NULL.
*/
plm_t *mpeg_player_get_decoder(mpeg_player_t *player);

/** \brief   Seek to a specific time in the MPEG stream.
    \ingroup mpeg_playback

    This function seeks the MPEG decoder to a specific time position.
    Useful for frame-by-frame playback.

    \param  player      The MPEG player instance.
    \param  time        Time in seconds to seek to.
    \param  seek_exact  If non-zero, seek to exact frame (slower but more accurate).
    \return             Non-zero on success, zero on failure.
*/
int mpeg_player_seek(mpeg_player_t *player, double time, int seek_exact);

/** \brief   Decode a single video frame.
    \ingroup mpeg_playback

    This function decodes the next video frame from the MPEG stream.
    The returned frame is valid until the next call to this function.

    \param  player      The MPEG player instance.
    \return             Pointer to decoded frame, or NULL if no frame available.
*/
plm_frame_t *mpeg_player_decode_video(mpeg_player_t *player);

/** \brief   Set the current decoded frame (for YUV rendering).
    \param  player      The MPEG player instance.
    \param  frame       The decoded frame to set.
*/
void mpeg_player_set_frame(mpeg_player_t *player, plm_frame_t *frame);

/** \brief   Get the current decoded frame (for YUV rendering).
    \param  player      The MPEG player instance.
    \return             The current frame, or NULL if none.
*/
plm_frame_t *mpeg_player_get_frame(mpeg_player_t *player);

/** \brief   Get video width.
    \param  player      The MPEG player instance.
    \return             Video width in pixels, or 0 on error.
*/
int mpeg_player_get_width(mpeg_player_t *player);

/** \brief   Get video height.
    \param  player      The MPEG player instance.
    \return             Video height in pixels, or 0 on error.
*/
int mpeg_player_get_height(mpeg_player_t *player);

/** \brief   Get video framerate.
    \param  player      The MPEG player instance.
    \return             Framerate in frames per second, or 0.0 on error.
*/
double mpeg_player_get_framerate(mpeg_player_t *player);

/** \brief   Get video duration.
    \param  player      The MPEG player instance.
    \return             Duration in seconds, or 0.0 on error.
*/
double mpeg_player_get_duration(mpeg_player_t *player);

/** \brief   Get number of audio streams.
    \param  player      The MPEG player instance.
    \return             Number of audio streams, or 0 on error.
*/
int mpeg_player_get_num_audio_streams(mpeg_player_t *player);

/** \brief   Get audio sample rate.
    \param  player      The MPEG player instance.
    \return             Sample rate in Hz, or 0 on error.
*/
int mpeg_player_get_samplerate(mpeg_player_t *player);

/** \brief   Start audio streaming.
    \param  player      The MPEG player instance.
    \return             Non-zero on success, zero on error.
*/
int mpeg_player_start_audio(mpeg_player_t *player);

/** \brief   Poll audio stream (call this every frame).
    \param  player      The MPEG player instance.
*/
void mpeg_player_poll_audio(mpeg_player_t *player);

/** \brief   Stop audio streaming.
    \param  player      The MPEG player instance.
*/
void mpeg_player_stop_audio(mpeg_player_t *player);

#ifdef __cplusplus
}
#endif

#endif
