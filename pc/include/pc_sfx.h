/* pc_sfx.h - PC-side synthesized sound effects (debug car engine, horn)
 *
 * The game's sound bank has no engine or horn, so these are generated on
 * the audio thread and mixed into the output in AIInitDMA. The game thread
 * only sets parameters; everything is lock-free. */
#ifndef PC_SFX_H
#define PC_SFX_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Engine running or not, and its revs (0 = idle, 1 = flat out). */
void pc_sfx_car_engine(int on, f32 revs);
/* A short throttle blip (start-up "vroom"). */
void pc_sfx_car_rev(void);
/* Horn held down. */
void pc_sfx_car_horn(int on);
/* Tyre squeal level, 0..1. */
void pc_sfx_car_skid(f32 level);

/* Audio thread: add the effects to n interleaved stereo frames at rate Hz.
 * vol is the master volume, 0..100. */
void pc_sfx_mix(s16* stereo, int n, int rate, int vol);

#ifdef __cplusplus
}
#endif

#endif /* PC_SFX_H */
