/*
 * AicaDsp.cpp - AICA Hardware EFSDSP for Xash3D Dreamcast room effects
 *
 * AUTHORITATIVE REGISTER LAYOUT (neil_corlett_aica_notes.txt):
 *
 * === MPRO ===
 *   128 instructions, each 64-bit, stored as 4 × 32-bit registers (lower 16 used):
 *   0x3400 = bits [63:48] of instruction 0  (word 0: TRA[6:0], TWT, TWA[6:0])
 *   0x3404 = bits [47:32] of instruction 0  (word 1: XSEL, YSEL[1:0], IRA[5:0], IWT, IWA[4:0])
 *   0x3408 = bits [31:16] of instruction 0  (word 2: TABLE,MWT,MRD,EWT,EWA[3:0],ADRL,FRCL,SHFT[1:0],YRL,NEGB,ZERO,BSEL)
 *   0x340C = bits [15:0]  of instruction 0  (word 3: NOFL, MASA[5:0], ADREB, NXADR)
 *   0x3410 = bits [63:48] of instruction 1
 *   ...
 *   0x3BFC = bits [15:0]  of instruction 127  ← TRIGGER (write last to start DSP)
 *   Data is in the LOWER 16 bits of each 32-bit slot.
 *
 * === IRA encoding ===
 *   0x00-0x1F: MEMS[IRA]      (internal 24-bit data registers)
 *   0x20-0x2F: MIXS[IRA-0x20] (per-channel send buses)
 *   0x30-0x31: EXTS[IRA-0x30] (CDDA left/right)
 *   → IRA=0x20 reads MIXS[0] (all channels with ISEL=0)
 *
 * === COEF / MADRS ===
 *   COEF:  0x3000, 0x3004, ..., 0x31FC  (stride 4, lower 16, bits 15-3 = 13-bit value)
 *   MADRS: 0x3200, 0x3204, ..., 0x32FC  (stride 4, lower 16, full 16-bit address)
 *
 * === Per-channel registers ===
 *   ch*0x80 + 0x20: DSPChannelSend     [7:4]=IMXL, [3:0]=ISEL  (DSP effect send)
 *   ch*0x80 + 0x24: DirectPanVolSend   [11:8]=DISDL, [4:0]=DIPAN (direct output)
 *   → Write IMXL/ISEL to 0x20. KOS ARM7 handles DISDL/DIPAN at 0x24.
 *
 * === EFSPAN (0x2000-0x203C) ===
 *   [11:8] = EFSDL (0x0-0xF, 0xF=full volume)
 *   [4:0]  = EFPAN (0x10=center)
 *   → Full volume center: 0x0F10
 *
 * === RingBuffer register (0x2804) ===
 *   [14:13] = RBL (ring size: 0=8K,1=16K,2=32K,3=64K words)
 *   [11:0]  = RBP (bits 22-11 of ARAM byte address; each unit = 2 KB)
 *
 * === 4-step reverb echo program ===
 *   Step 0 (even): INPUT=MIXS[0] (IRA=0x20), X=input, Y=COEF[0]≈1.0, ZERO→B=0
 *                  TWT=1 → TEMP[0+DEC] = SHIFTED ≈ input
 *   Step 1 (odd) : MRD from ring[MADRS[1]+DEC] (start reading delayed sample)
 *                  ZERO=1 → no useful accumulation needed
 *   Step 2 (even): INPUT=MEMS[0] (IRA=0x00), X=MEMS[0]=delayed (from prev cycle)
 *                  Y=COEF[2]=feedback, B=TEMP[0+DEC]=input
 *                  ACC = delayed*feedback + input  (echo formula)
 *   Step 3 (odd) : IWT→MEMS[0] (stores MRD from step1, 2 steps ago) ✓
 *                  SHIFTED = sat(ACC_step2) = echo signal
 *                  EWT → EFREG[0] += SHIFTED>>8   (reverb output to DAC)
 *                  MWT → ring[MADRS[0]+DEC] = SHIFTED (close feedback loop)
 */

#include <kos.h>
#include <dc/sound/sound.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "AicaDsp.h"

/* Readable DSP internal registers (accessible to SH-4 via G2 bus):
 *   MIXS[0] bits [19:4] at 0x4504  (20-bit mix bus, upper 16 bits readable)
 *   EFREG[0] at 0x4580              (16-bit effect output)
 * Temporarily readable for diagnostics; in production these aren't needed. */
#define AICA_MIXS0_HI   (AICA_BASE + 0x4504u)   /* bits [19:4] of MIXS[0] */
#define AICA_EFREG(n)   (AICA_BASE + 0x4580u + (uint32_t)(n)*4u)
#define RD16(addr)      (*((volatile uint16_t *)(addr)))

/* =========================================================================
 * Register addresses — SH-4 uncached G2 view of AICA (0xa0700000)
 * ====================================================================== */
#define AICA_BASE           0xa0700000u

/* EFSPAN[n]: effect output level/pan.  Stride 4, starts at 0x2000.        */
#define AICA_EFSPAN(n)      (AICA_BASE + 0x2000u + (uint32_t)(n) * 4u)

/* Ring-buffer config register at 0x2804: [14:13]=RBL, [11:0]=RBP          */
#define AICA_RINGBUF_REG    (AICA_BASE + 0x2804u)

/* DSP coefficient table: COEF(s) at lower 16 of 32-bit slot, stride 4     */
#define AICA_DSP_COEF_BASE  (AICA_BASE + 0x3000u)
/* DSP address registers: MADRS(a) at lower 16 of 32-bit slot, stride 4    */
#define AICA_DSP_MADRS_BASE (AICA_BASE + 0x3200u)
/* DSP microprogram: word[n] of step[s] at lower 16 of 32-bit slot         *
 *   addr = MPRO_BASE + s*16 + n*4                                          */
#define AICA_DSP_MPRO_BASE  (AICA_BASE + 0x3400u)
/* Trigger = lower 16 bits of last MPRO slot (step127, word3)               */
#define AICA_DSP_TRIGGER    (AICA_BASE + 0x3BFCu)

/* Per-channel DSP send register: 0x20 = [7:4]IMXL [3:0]ISEL               */
#define AICA_CH_DSP_SEND(n) (AICA_BASE + (uint32_t)(n) * 0x80u + 0x20u)

/* 32-bit G2 bus writes — the safe/correct width for AICA register space.
 * Writing as 16-bit on the G2 bus can have unexpected byte-ordering effects
 * on SH-4 (little-endian). Always use 32-bit; the 16-bit value goes into
 * the lower half of the 32-bit slot (upper half written as 0).             */
#define WR32(addr, val)  (*((volatile uint32_t *)(addr)) = (uint32_t)(val))
#define WR16 WR32       /* alias: keep call-sites unchanged */

/* MPRO addressing helpers:
 *   step s, IPtr word w  →  MPRO_BASE + s*16 + w*4  (lower 16 of 32-bit) */
#define DSP_MPRO_WORD(step, w) \
    (AICA_DSP_MPRO_BASE + (uint32_t)(step)*16u + (uint32_t)(w)*4u)

/* COEF for step s: COEF_BASE + s*4  (lower 16 bits, bits 15-3 = 13-bit Y) */
#define DSP_COEF(step)   (AICA_DSP_COEF_BASE  + (uint32_t)(step)*4u)
/* MADRS entry a:   MADRS_BASE + a*4 (lower 16 bits, 16-bit address)        */
#define DSP_MADRS(a)     (AICA_DSP_MADRS_BASE + (uint32_t)(a) *4u)

/* =========================================================================
 * Coefficient encoding
 *   COEF register bits [15:3] = 13-bit signed Y.
 *   to store value V (range -4096..+4095): write V<<3 to the register.
 *   ACC += (X × Y) >> 12  →  UNITY(Y=4095): factor ≈ 0.9998
 * ====================================================================== */
#define COEF_UNITY  0x7FF8u   /* Y = 4095 ≈ 1.0  (0x7FF8 >> 3 = 4095) */
#define COEF_ZERO   0x0000u

static uint16_t feedback_to_coef(float f)
{
    int32_t y = (int32_t)(f * 4096.0f);
    if (y >  4095) y =  4095;
    if (y < -4096) y = -4096;
    return (uint16_t)(int16_t)(y << 3);   /* store in bits [15:3] */
}

/* =========================================================================
 * Ring-buffer dimensions
 *   16 K words = 32 KB  →  16384 / 44100 ≈ 371 ms max delay.
 * ====================================================================== */
#define DSP_RING_WORDS   16384u
#define DSP_RING_BYTES   (DSP_RING_WORDS * 2u)   /* 32 KB */
#define DSP_RBL_FIELD    1u                       /* 1 = 16384 words       */
#define DSP_AICA_SR      44100

static uint32_t s_ring_aica_addr = 0;
static int      s_dsp_ready      = 0;
static int      s_route_log_count = 0;   /* rate-limit RouteChannel prints */

/* =========================================================================
 * Preset table – 29 entries mapping room_type 0..28 to hardware comb-filter
 * parameters (delay_s, feedback) for a SIMPLE SINGLE-TAP comb filter.
 *
 * Two categories in the PC rgsxpre[] table:
 *
 * ECHO rooms (size=0, refl=0): rooms 1-4, 20-22, 27-28 partial.
 *   PC uses a simple delay+feedback chain which maps 1:1 to our hardware.
 *   These are copied directly from rgsxpre[].delay / .feedback.
 *
 * REVERB rooms (size>0): rooms 5-13, 17-19, 23-25.
 *   PC uses a multi-tap comb reverb where refl is applied once per buffer
 *   pass, giving PC T60 = size * log(0.001)/log(refl) = 2-6 s.  This sounds
 *   natural on PC because diffusion hides discrete echoes.  At the same T60
 *   our single-tap comb produces very obvious discrete repeats.  We target
 *   ~15% of the PC T60 to match the PERCEIVED density.  Formula:
 *     hw_fb = pow(10, -3 * delay_s / T60_target)
 *
 * T60 formula: T60 = delay_s * log(0.001) / log(fb)
 * ====================================================================== */
typedef struct { float feedback; float delay_s; } aica_preset_t;

static const aica_preset_t s_presets[29] = {
 /* id  room        feedback  delay_s  source / T60 note                         */
 /*  0  off      */ { 0.00f, 0.000f }, /* silence                                */
 /*  1  generic  */ { 0.10f, 0.065f }, /* PC echo: d=0.065 fb=0.10   T60≈0.20s  */
 /*  2  metallic */ { 0.75f, 0.020f }, /* PC echo: d=0.020 fb=0.75   T60≈0.10s  */
 /*  3           */ { 0.78f, 0.030f }, /* PC echo: d=0.030 fb=0.78   T60≈0.14s  */
 /*  4           */ { 0.77f, 0.060f }, /* PC echo: d=0.060 fb=0.77   T60≈0.28s  */
 /*  5  tunnel   */ { 0.58f, 0.025f }, /* PC reverb T60≈2.1s → hw≈0.32s 15%    */
 /*  6           */ { 0.66f, 0.025f }, /* PC reverb T60≈2.8s → hw≈0.42s 15%    */
 /*  7           */ { 0.70f, 0.030f }, /* PC reverb T60≈3.9s → hw≈0.59s 15%    */
 /*  8  chamber  */ { 0.56f, 0.025f }, /* PC reverb T60≈2.0s → hw≈0.30s 15%    */
 /*  9           */ { 0.55f, 0.030f }, /* PC reverb T60≈3.3s → hw≈0.35s ~10%   */
 /* 10           */ { 0.71f, 0.025f }, /* PC reverb T60≈6.6s → hw≈0.55s  8%    */
 /* 11  brite    */ { 0.50f, 0.020f }, /* PC reverb T60≈1.0s → hw≈0.20s 20%    */
 /* 12           */ { 0.50f, 0.025f }, /* PC reverb T60≈1.4s → hw≈0.25s 17%    */
 /* 13           */ { 0.55f, 0.025f }, /* PC reverb T60≈2.3s → hw≈0.30s 13%    */
 /* 14  water-LP */ { 0.00f, 0.000f }, /* PC: lp=1, no echo (just LP texture)   */
 /* 15  water    */ { 0.44f, 0.060f }, /* PC echo d=0.060 fb=0.85 → damp for lp */
 /* 16  water    */ { 0.42f, 0.100f }, /* PC echo d=0.200 fb=0.60 → shorter     */
 /* 17  concrete */ { 0.50f, 0.025f }, /* PC reverb T60≈1.5s → hw≈0.25s 16%    */
 /* 18           */ { 0.55f, 0.030f }, /* PC reverb T60≈3.3s → hw≈0.35s ~10%   */
 /* 19           */ { 0.48f, 0.060f }, /* reverb+echo both long → hw≈0.43s      */
 /* 20  outside  */ { 0.42f, 0.300f }, /* PC echo: d=0.300 fb=0.42 (direct)     */
 /* 21           */ { 0.48f, 0.350f }, /* PC echo: d=0.350 fb=0.48 (direct)     */
 /* 22           */ { 0.60f, 0.365f }, /* PC echo: d=0.380 fb=0.60 capped@371ms */
 /* 23  cavern   */ { 0.37f, 0.100f }, /* reverb+echo mixed → hw≈0.52s          */
 /* 24           */ { 0.36f, 0.150f }, /* reverb+echo → hw≈0.56s               */
 /* 25           */ { 0.40f, 0.200f }, /* reverb+echo → hw≈0.95s               */
 /* 26  weirdo   */ { 0.50f, 0.010f }, /* PC reverb mod=1 → flutter T60≈0.14s  */
 /* 27  pipe     */ { 0.75f, 0.005f }, /* PC d=0.009→0.005s, tighter flutter    */
 /* 28  long cvn */ { 0.55f, 0.200f }, /* PC: refl=0.999+d=0.200 → T60≈2.3s    */
};

/* =========================================================================
 * MPRO helpers
 *   Each instruction is 64 bits, split across 4 hardware 32-bit registers.
 *   Data is always in the LOWER 16 bits of each 32-bit slot.
 *   Upper 16 bits of every slot must be zero (written explicitly).
 *
 *   p0 = bits [63:48]: TRA[15:9] | TWT[8] | TWA[7:1]
 *   p1 = bits [47:32]: XSEL[15] | YSEL[14:13] | IRA[12:7] | IWT[6] | IWA[5:1]
 *   p2 = bits [31:16]: TABLE[15]|MWT[14]|MRD[13]|EWT[12]|EWA[11:8]|
 *                      ADRL[7]|FRCL[6]|SHFT[5:4]|YRL[3]|NEGB[2]|ZERO[1]|BSEL[0]
 *   p3 = bits [15:0]:  NOFL[15] | MASA[14:9] | ADREB[8] | NXADR[7]
 * ====================================================================== */
static void mpro_step(int s,
                      uint16_t p0, uint16_t p1,
                      uint16_t p2, uint16_t p3)
{
    uint32_t base = AICA_DSP_MPRO_BASE + (uint32_t)s * 16u;
    /* Data in LOWER 16 bits of each 32-bit slot.  Written as 32-bit to
     * avoid potential G2-bus byte-ordering issues with 16-bit accesses.    */
    WR32(base +  0u, p0);   /* word 0: bits 63-48 */
    WR32(base +  4u, p1);   /* word 1: bits 47-32 */
    WR32(base +  8u, p2);   /* word 2: bits 31-16 */
    WR32(base + 12u, p3);   /* word 3: bits 15-0  */
}

static void mpro_clear_all(void)
{
    for (int i = 0; i < 128; i++)
        mpro_step(i, 0, 0, 0, 0);
    /* Writing the last MPRO register commits the program to the DSP.        */
    WR16(AICA_DSP_TRIGGER, 0);
}

/* Upload the 4-step reverb/echo microprogram.
 *
 * Step 0 (even): read input from MIXS[0]; NO TWT yet (SHIFTED here is
 *   sat(ACC_step127) from previous sample, not the live input).
 *   p0 = 0x0000          (TWT=0 — no temp write)
 *   p1 = XSEL=1(input), YSEL=1(COEF[0]=unity), IRA=0x20(MIXS[0])  = 0xB000
 *   p2 = ZERO=1          (B=0)                                       = 0x0002
 *   p3 = 0x0000
 *   ACC_step0 = MIXS[0] * COEF[0] / 4096  ≈  MIXS[0]  (live input)
 *
 * Step 1 (odd): capture live input to TEMP, start ring-buffer read.
 *   p0 = TWT=1, TWA=0   → (1<<8) = 0x0100
 *       [SHIFTED_step1 = sat(ACC_step0) ≈ live input → TEMP[DEC] = input]
 *   p1 = 0x0000          (XSEL=0, YSEL=0, ZERO handled by p2)
 *   p2 = MRD=1, ZERO=1  → (1<<13)|(1<<1) = 0x2002
 *       [start ring read from MADRS[1]+DEC]
 *   p3 = MASA=1          → (1<<9) = 0x0200
 *
 * Step 2 (even): compute  ACC = MEMS[0]*feedback + input
 *   p0 = 0x0000  (TRA=0 → B = TEMP[0+DEC] = input stored by step 1)
 *   p1 = XSEL=1(input), YSEL=1(COEF[2]=feedback), IRA=0x00(MEMS[0])= 0xA000
 *   p2 = 0x0000  (BSEL=0, ZERO=0 → B = TEMP[DEC] = live input)
 *   p3 = 0x0000
 *
 * Step 3 (odd): commit — write to ring & DAC output
 *   p0 = 0x0000
 *   p1 = IWT=1, IWA=0   → (1<<6) = 0x0040
 *       [IWT copies MRD result from step 1 (2 steps ago) → MEMS[0]]
 *   p2 = MWT=1, EWT=1, EWA=0  → (1<<14)|(1<<12) = 0x5000
 *       SHIFTED_step3 = sat(ACC_step2) = delayed*fb + input
 *       EWT: EFREG[0] = SHIFTED>>8   (→ DAC via EFSPAN)
 *       MWT: ring[MADRS[0]+DEC] = float(SHIFTED)   (close feedback loop)
 *   p3 = MASA=0, NOFL=0  = 0x0000
 */
static void upload_reverb_program(void)
{
    /*           p0      p1      p2      p3   */
    mpro_step(0, 0x0000, 0xB000, 0x0002, 0x0000);  /* step 0: MAC input, no TWT */
    mpro_step(1, 0x0100, 0x0000, 0x2002, 0x0200);  /* step 1: TWT(input)+MRD   */
    mpro_step(2, 0x0000, 0xA000, 0x0000, 0x0000);  /* step 2: delayed*fb+input */
    mpro_step(3, 0x0000, 0x0040, 0x5000, 0x0000);  /* step 3: IWT+MWT+EWT      */
    /* Steps 4-127 zeroed by mpro_clear_all().
     * Write the trigger (last MPRO slot) to commit the program.            */
    WR32(AICA_DSP_TRIGGER, 0);
}

/* =========================================================================
 * Public API
 * ====================================================================== */

void AICA_DSP_Init(void)
{
    s_dsp_ready = 0;

    /* Allocate ring buffer from ARAM; align to 2 KB for RBP granularity.  */
    uint32_t raw = snd_mem_malloc(DSP_RING_BYTES + 2048u);
    if (!raw) {
        return;
    }
    s_ring_aica_addr = (raw + 2047u) & ~2047u;

    /* Zero the ring buffer */
    spu_memset(s_ring_aica_addr, 0, DSP_RING_BYTES);

    /* Stop DSP and zero all MPRO / COEF / MADRS */
    mpro_clear_all();
    for (int i = 0; i < 128; i++) WR16(DSP_COEF(i),  0);
    for (int i = 0; i <  64; i++) WR16(DSP_MADRS(i), 0);

    /* Static COEF values:
     *   COEF[0] = unity (step 0: pass input through with ≈ 1.0 gain)
     *   COEF[2] = 0 initially; updated per-room via SetRoomType/ApplyPreset */
    WR16(DSP_COEF(0), COEF_UNITY);
    WR16(DSP_COEF(1), COEF_ZERO);
    WR16(DSP_COEF(2), COEF_ZERO);
    WR16(DSP_COEF(3), COEF_ZERO);

    /* MADRS[0] = 0: the ring write head is always at MDEC (DEC counter).
     * MADRS[1] = 0: updated per-room (delay tap in samples).              */
    WR16(DSP_MADRS(0), 0);
    WR16(DSP_MADRS(1), 0);

    /* Ring-buffer config: RBL=1 (16 K words), RBP = ring_addr >> 11       */
    uint32_t rbp = s_ring_aica_addr >> 11u;
    WR16(AICA_RINGBUF_REG,
         (uint16_t)((DSP_RBL_FIELD << 13) | (rbp & 0xFFFu)));

    /* EFSPAN[0]: route EFREG[0] to DAC at ~66 % of full, centre pan.
     *   bits [11:8] = EFSDL = 0xA (attenuated ~33 % from max, wet not too loud)
     *   bits [4:0]  = EFPAN = 0x10 (centre)
     * EFSDL=0xF was saturating / overpowering direct sound.               */
    WR16(AICA_EFSPAN(0), 0x0A10u);

    /* Pre-route ALL 64 channels to MIXS[0] (IMXL=15, ISEL=0).
     * We do this at init so channels started before RouteChannel is called
     * still feed the DSP.  KOS never touches register 0x20, so this sticks. */
    for (int i = 0; i < 64; i++)
        WR16(AICA_CH_DSP_SEND(i), (uint16_t)((15u << 4) | 0u));

    /* Upload the reverb microprogram and start the DSP. */
    upload_reverb_program();

    s_dsp_ready = 1;
}

void AICA_DSP_SetRoomType(int room_type)
{
    if (!s_dsp_ready) return;

    if (room_type < 0 || room_type >= 29) room_type = 0;

    if (room_type == 0) {
        WR16(DSP_COEF(2),  COEF_ZERO);
        WR16(DSP_MADRS(1), 0);
        return;
    }

    const aica_preset_t *p = &s_presets[room_type];
    float fb = (p->feedback > 0.98f) ? 0.98f : p->feedback;
    if (fb < 0.0f) fb = 0.0f;

    int delay_words = (int)(p->delay_s * (float)DSP_AICA_SR);
    if (delay_words < 1) delay_words = 1;
    if (delay_words >= (int)(DSP_RING_WORDS - 1))
        delay_words = (int)(DSP_RING_WORDS - 2);

    uint16_t coef_val = feedback_to_coef(fb);
    WR16(DSP_COEF(2),  coef_val);
    WR16(DSP_MADRS(1), (uint16_t)delay_words);

}
void AICA_DSP_ApplyPreset(int   room_type,
                           float lp,
                           float size,    float refl,
                           float delay,   float feedback)
{

    if (!s_dsp_ready) return;

    if (room_type <= 0) {
        AICA_DSP_SetRoomType(0);
        return;
    }
    if (room_type >= 29) room_type = 28;

    /* Always read from the tuned preset table (avoids naive refl/size mapping).
     * The raw sx_preset_t params (refl/size) are designed for a multi-tap
     * software reverb and must NOT be used directly as simple comb-filter
     * coefficients — they produce T60 ≈ 3–5 s instead of 0.3–0.9 s.       */
    const aica_preset_t *p = &s_presets[room_type];
    float fb      = p->feedback;
    float delay_s = p->delay_s;

    /* Water rooms (lp=1): further damp the feedback to approximate LP decay */
    if (lp > 0.5f) fb *= 0.55f;

    if (fb > 0.98f) fb = 0.98f;
    if (fb < 0.0f)  fb = 0.0f;

    int delay_words = (int)(delay_s * (float)DSP_AICA_SR);
    if (delay_words < 1) delay_words = 1;
    if (delay_words >= (int)(DSP_RING_WORDS - 1))
        delay_words = (int)(DSP_RING_WORDS - 2);

    uint16_t coef_val = feedback_to_coef(fb);
    WR32(DSP_COEF(2),  coef_val);
    WR32(DSP_MADRS(1), (uint16_t)delay_words);
}

/* Configure per-channel DSP routing.
 *
 * Register 0x20 (DSPChannelSend) — NOT touched by KOS ARM7 firmware:
 *   bits [7:4] = IMXL (0=no send, 0xF=full send to MIXS bus)
 *   bits [3:0] = ISEL (which MIXS bus: 0-15; we use 0)
 *
 * Note: we pre-route all channels at init; this just ensures routing is
 * correct for channels started after Init (e.g. after a level change).    */
void AICA_DSP_RouteChannel(int ch, int imxl, int isel)
{
    if (!s_dsp_ready || ch < 0 || ch > 63) return;
    uint16_t val = (uint16_t)(((imxl & 0xF) << 4) | (isel & 0xF));
    WR16(AICA_CH_DSP_SEND(ch), val);
}

/* Diagnostic: read EFREG[0] and MIXS[0] from hardware to confirm DSP activity.
 * Call periodically (e.g. every ~1 second) to see if signal is flowing.    */
void AICA_DSP_DebugPrint(void)
{
    if (!s_dsp_ready) return;
    uint16_t efreg = RD16(AICA_EFREG(0));
    uint16_t mixs  = RD16(AICA_MIXS0_HI);
}

void AICA_DSP_Disable(void)
{
    if (!s_dsp_ready) return;
    mpro_clear_all();
    WR16(DSP_COEF(2), COEF_ZERO);
}
