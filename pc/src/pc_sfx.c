/* pc_sfx.c - PC-side synthesized sound effects (debug car engine, horn)
 *
 * Parameters are published by the game thread as fixed-point atomics and
 * read by the audio thread, which smooths them per sample block so there
 * are no clicks. */
#include "pc_sfx.h"
#include "pc_platform.h"

#include <math.h>

#define FX_ONE 4096 /* fixed point for the atomics */

static SDL_atomic_t s_engine_on;
static SDL_atomic_t s_engine_revs; /* 0..FX_ONE */
static SDL_atomic_t s_rev_blip;    /* set to 1 to start a blip */
static SDL_atomic_t s_horn_on;
static SDL_atomic_t s_skid;        /* 0..FX_ONE */

void pc_sfx_car_engine(int on, f32 revs) {
    if (revs < 0.0f) revs = 0.0f;
    if (revs > 1.0f) revs = 1.0f;
    SDL_AtomicSet(&s_engine_revs, (int)(revs * FX_ONE));
    SDL_AtomicSet(&s_engine_on, on ? 1 : 0);
}

void pc_sfx_car_rev(void) {
    SDL_AtomicSet(&s_rev_blip, 1);
}

void pc_sfx_car_horn(int on) {
    SDL_AtomicSet(&s_horn_on, on ? 1 : 0);
}

void pc_sfx_car_skid(f32 level) {
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    SDL_AtomicSet(&s_skid, (int)(level * FX_ONE));
}

/* ---- audio thread state ---- */

static float s_eng_gain;   /* 0..1, follows engine on/off */
static float s_eng_revs;   /* smoothed revs */
static float s_eng_phase;  /* 0..1 */
static float s_eng_lp;     /* one-pole low-pass state */
static float s_blip;       /* throttle blip envelope */
static float s_horn_gain;
static float s_horn_ph1;
static float s_horn_ph2;
static float s_horn_lp;
static float s_skid_gain;
static float s_skid_bp1;
static float s_skid_bp2;
static unsigned s_rng = 0x9E3779B9u;

static float frand(void) {
    s_rng = s_rng * 1664525u + 1013904223u;
    return (float)(int)(s_rng >> 9) * (1.0f / 4194304.0f) - 1.0f; /* -1..1 */
}

/* Move x toward target by at most step. */
static float slew(float x, float target, float step) {
    if (x < target) {
        x += step;
        if (x > target) x = target;
    } else if (x > target) {
        x -= step;
        if (x < target) x = target;
    }
    return x;
}

void pc_sfx_mix(s16* stereo, int n, int rate, int vol) {
    int engine_on = SDL_AtomicGet(&s_engine_on);
    float revs_target = (float)SDL_AtomicGet(&s_engine_revs) / FX_ONE;
    int horn_on = SDL_AtomicGet(&s_horn_on);
    float skid_target = (float)SDL_AtomicGet(&s_skid) / FX_ONE;
    float inv_rate = 1.0f / (float)rate;
    float master;
    int i;

    if (SDL_AtomicSet(&s_rev_blip, 0)) {
        s_blip = 1.0f;
    }

    if (!engine_on && s_eng_gain <= 0.0f && !horn_on && s_horn_gain <= 0.0f && s_skid_gain <= 0.0f &&
        skid_target <= 0.0f) {
        return; /* silent: nothing to mix */
    }

    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    master = (float)vol / 100.0f * 32767.0f;

    for (i = 0; i < n; i++) {
        float out = 0.0f;

        /* ---- engine: saw-ish harmonics, low-passed, 4-stroke chug at idle ---- */
        s_eng_gain = slew(s_eng_gain, engine_on ? 1.0f : 0.0f, engine_on ? 2.5f * inv_rate : 1.5f * inv_rate);
        s_eng_revs = slew(s_eng_revs, revs_target, 1.2f * inv_rate);
        s_blip = slew(s_blip, 0.0f, 1.3f * inv_rate);
        if (s_eng_gain > 0.0f) {
            float revs = s_eng_revs + s_blip * 0.55f;
            float freq = 32.0f + 95.0f * revs;
            float p;
            float saw;
            float chug;
            float v;

            if (revs > 1.0f) revs = 1.0f;
            s_eng_phase += freq * inv_rate;
            if (s_eng_phase >= 1.0f) s_eng_phase -= 1.0f;
            p = s_eng_phase;
            saw = 2.0f * p - 1.0f;
            /* second, slightly detuned saw gives the rumble some beat */
            saw += 0.6f * (2.0f * fmodf(p * 1.012f + 0.3f, 1.0f) - 1.0f);
            /* half-speed pulse: cylinders firing, strongest at idle */
            chug = (fmodf(p * 0.5f + 0.25f, 1.0f) < 0.5f) ? 1.0f : 0.55f + 0.45f * revs;
            v = saw * chug;
            /* low-pass opens up with the revs */
            s_eng_lp += (v - s_eng_lp) * (0.08f + 0.22f * revs);
            out += s_eng_lp * s_eng_gain * (0.075f + 0.06f * revs);
        }

        /* ---- horn: two detuned square-ish tones, softened ---- */
        s_horn_gain = slew(s_horn_gain, horn_on ? 1.0f : 0.0f, 40.0f * inv_rate);
        if (s_horn_gain > 0.0f) {
            float v;

            s_horn_ph1 += 392.0f * inv_rate; /* G4 */
            s_horn_ph2 += 466.0f * inv_rate; /* A#4 */
            if (s_horn_ph1 >= 1.0f) s_horn_ph1 -= 1.0f;
            if (s_horn_ph2 >= 1.0f) s_horn_ph2 -= 1.0f;
            v = (s_horn_ph1 < 0.5f ? 1.0f : -1.0f) + (s_horn_ph2 < 0.5f ? 1.0f : -1.0f);
            s_horn_lp += (v - s_horn_lp) * 0.12f;
            out += s_horn_lp * s_horn_gain * 0.14f;
        }

        /* ---- tyre squeal: band-passed noise ---- */
        s_skid_gain = slew(s_skid_gain, skid_target, 6.0f * inv_rate);
        if (s_skid_gain > 0.0f) {
            float v = frand();

            /* two cascaded one-poles around ~1.6 kHz */
            s_skid_bp1 += (v - s_skid_bp1) * 0.28f;
            s_skid_bp2 += (s_skid_bp1 - s_skid_bp2) * 0.28f;
            out += (s_skid_bp1 - s_skid_bp2) * s_skid_gain * 0.35f;
        }

        if (out != 0.0f) {
            int add = (int)(out * master);
            int l = stereo[2 * i] + add;
            int r = stereo[2 * i + 1] + add;

            if (l > 32767) l = 32767;
            if (l < -32768) l = -32768;
            if (r > 32767) r = 32767;
            if (r < -32768) r = -32768;
            stereo[2 * i] = (s16)l;
            stereo[2 * i + 1] = (s16)r;
        }
    }
}
