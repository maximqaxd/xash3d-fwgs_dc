/*
 * AicaDsp.h - AICA Hardware DSP programming for Xash3D room effects
 *
 * The AICA EFSDSP runs a user-supplied 128-step microprogram every sample,
 * reading ARAM-backed delay lines and writing to EFREG effect outputs.
 * This replaces the software s_dsp.c paint-buffer processing (unavailable
 * on Dreamcast) with zero-CPU-cost hardware reverb/echo.
 *
 * AICA register offsets (relative to SH-4 uncached view at 0xa0700000):
 *   0x2000  EFSPAN[0..17]  – effect output level/pan (stride 4)
 *   0x2800  Global regs    – MVOL/RBP/RBL at +0x04
 *   0x3000  COEF[step]     – step coefficient (stride 4 per step)
 *   0x3200  MADRS[a]       – ring-buffer address entries (stride 4)
 *   0x3400  MPRO           – microprogram (16 bytes / step)
 *   0x3BFC  DSP trigger    – write last MPRO word to (re)start DSP
 *   ch*0x80+0x20  IMXL/ISEL  per-channel DSP send routing (NOT 0x24!)
 *   ch*0x80+0x24  DISDL/DIPAN direct output (set by KOS ARM7)
 */
#ifndef AICA_DSP_H
#define AICA_DSP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize AICA DSP: allocate 16 K-word ARAM ring buffer, zero all DSP
 * state, upload 4-step reverb microprogram and start the DSP.
 * Call once after snd_init() during AudioEngine startup.                  */
void AICA_DSP_Init(void);

/* Switch to a named room preset (0-28, matching room_type cvar).
 * Only updates COEF[2] (feedback) and MADRS[1] (delay); the microprogram
 * is not re-uploaded so there is no audio glitch.                         */
void AICA_DSP_SetRoomType(int room_type);

/* Apply a room effect derived directly from the sx_preset_t parameters
 * (lp, size, refl, delay, feedback).  Used from CheckNewDspPresets so
 * that both normal and alpha preset tables are handled automatically.
 *   lp       = room_lp   (1.0 = lowpass/water; reduces effective feedback)
 *   size     = room_size (reverb pre-delay length, seconds equivalent)
 *   refl     = room_refl (reverb decay coefficient)
 *   delay    = room_delay (echo delay time, seconds)
 *   feedback = room_feedback (echo decay coefficient)                     */
void AICA_DSP_ApplyPreset(int   room_type,
                           float lp,
                           float size,     float refl,
                           float delay,    float feedback);

/* Configure per-channel effect routing; call after aica_play_chn().
 *   ch   : AICA hardware channel (0-63)
 *   imxl : DSP send level 0-15  (0 = bypass, 15 = full send)
 *   isel : MIXS input bus 0-15  (use 0 for mono reverb bus)              */
void AICA_DSP_RouteChannel(int ch, int imxl, int isel);

/* Stop the DSP (zero microprogram, let ring-buffer tail decay). */
void AICA_DSP_Disable(void);

/* Debug: read EFREG[0] and MIXS[0] from hardware and printf them.
 * Non-zero EFREG[0] proves the DSP program is running and producing output.
 * Non-zero MIXS[0] proves at least one channel is sending audio to the bus.
 * Call periodically (e.g. once per second) while audio is playing.        */
void AICA_DSP_DebugPrint(void);

#ifdef __cplusplus
}
#endif
#endif /* AICA_DSP_H */
