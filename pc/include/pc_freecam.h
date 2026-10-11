/* pc_freecam.h - free camera: orbit and zoom the field camera with the C-stick */
#ifndef PC_FREECAM_H
#define PC_FREECAM_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_play_s;

extern int g_pc_freecam_on;

/* Flip the free camera on/off (the FreeCam key: C / right stick click). */
void pc_freecam_toggle(void);

/* FALSE while a text field, the settings menu or a non-game screen owns
 * the keyboard/pad. */
int pc_freecam_toggle_allowed(void);

/* Current orbit offsets: yaw added to the camera's angle, and the distance
 * factor (1 = the game's own distance). */
s16 pc_freecam_yaw(void);
f32 pc_freecam_zoom(void);
s16 pc_freecam_pitch(void);

/* Per-frame: reads the C-stick and updates the offsets. */
void pc_freecam_update(struct game_play_s* play);

#ifdef __cplusplus
}
#endif

#endif /* PC_FREECAM_H */
