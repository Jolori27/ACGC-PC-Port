/* pc_freecam.c - free camera: orbit and zoom the field camera with the C-stick
 *
 * While the free camera is on, the C-stick (right stick / arrow keys) no
 * longer steps the game's three fixed camera presets. Instead it adds a
 * continuous yaw offset and a distance factor to the goal that the normal
 * field camera chases every frame, so the game's own smoothing, collision
 * and culling keep working. Pitch follows the distance: the closer the
 * camera, the lower it gets, like the game's own close/far presets. */
#include "pc_freecam.h"
#include "pc_platform.h"
#include "pc_settings_menu.h"
#include "pc_typing.h"
#include "pc_pause_menu.h"

#include "m_play.h"
#include "m_common_data.h"
#include "m_camera2.h"
#include "m_submenu.h"

int g_pc_freecam_on;

/* Raw C-stick, written by PADRead in pc_pad.c every poll (-128..127). */
extern s8 g_pc_cstick_x;
extern s8 g_pc_cstick_y;

#define FREECAM_YAW_SPEED (DEG2SHORT_ANGLE(1.75f)) /* per 60fps frame at full deflection */
#define FREECAM_ZOOM_SPEED 0.014f                   /* distance factor per frame */
#define FREECAM_ZOOM_MIN 0.45f
#define FREECAM_ZOOM_MAX 1.75f
/* Pitch added at the extremes of the zoom range (closer = lower camera). */
#define FREECAM_PITCH_NEAR (DEG2SHORT_ANGLE(-24.0f))
#define FREECAM_PITCH_FAR (DEG2SHORT_ANGLE(22.0f))

static s16 s_yaw;
static f32 s_zoom = 1.0f;
static f32 s_stick_x; /* smoothed stick, so key taps don't jerk the camera */
static f32 s_stick_y;

void pc_freecam_toggle(void) {
    g_pc_freecam_on = !g_pc_freecam_on;
    s_yaw = 0;
    s_zoom = 1.0f;
    s_stick_x = 0.0f;
    s_stick_y = 0.0f;
}

int pc_freecam_toggle_allowed(void) {
    return !pc_settings_menu_active() && !g_pc_typing_mode && !g_pc_editor_active && !g_pc_nes_active &&
           !g_pc_title_main_menu_visible && !g_pc_model_viewer;
}

s16 pc_freecam_yaw(void) {
    return g_pc_freecam_on ? s_yaw : 0;
}

f32 pc_freecam_zoom(void) {
    return g_pc_freecam_on ? s_zoom : 1.0f;
}

s16 pc_freecam_pitch(void) {
    f32 t;

    if (!g_pc_freecam_on) {
        return 0;
    }

    if (s_zoom < 1.0f) {
        t = (1.0f - s_zoom) / (1.0f - FREECAM_ZOOM_MIN);
        return (s16)(t * (f32)FREECAM_PITCH_NEAR);
    }

    t = (s_zoom - 1.0f) / (FREECAM_ZOOM_MAX - 1.0f);
    return (s16)(t * (f32)FREECAM_PITCH_FAR);
}

void pc_freecam_update(GAME_PLAY* play) {
    f32 dt;
    f32 tx;
    f32 ty;

    if (!g_pc_freecam_on || play == NULL) {
        return;
    }

    /* Only steer while the field camera is the one in control and no menu
     * owns the C-stick. */
    if (play->camera.now_main_index != CAMERA2_PROCESS_NORMAL || play->submenu.process_status != mSM_PROCESS_WAIT ||
        g_pc_paused || pc_settings_menu_active() || g_pc_typing_mode) {
        s_stick_x = 0.0f;
        s_stick_y = 0.0f;
        return;
    }

    dt = (f32)play->game.graph->dt_num_60fps_frames;
    if (dt <= 0.0f) {
        dt = 1.0f;
    }
    if (dt > 4.0f) {
        dt = 4.0f;
    }

    tx = (f32)g_pc_cstick_x / 127.0f;
    ty = (f32)g_pc_cstick_y / 127.0f;
    if (tx > 1.0f) tx = 1.0f;
    if (tx < -1.0f) tx = -1.0f;
    if (ty > 1.0f) ty = 1.0f;
    if (ty < -1.0f) ty = -1.0f;

    s_stick_x += (tx - s_stick_x) * (1.0f - powf(0.75f, dt));
    s_stick_y += (ty - s_stick_y) * (1.0f - powf(0.75f, dt));

    /* Stick right orbits the camera to the right around the player. */
    s_yaw = (s16)(s_yaw - (s16)(s_stick_x * (f32)FREECAM_YAW_SPEED * dt));

    /* Stick up zooms in, down zooms out. */
    s_zoom -= s_stick_y * FREECAM_ZOOM_SPEED * dt;
    if (s_zoom < FREECAM_ZOOM_MIN) s_zoom = FREECAM_ZOOM_MIN;
    if (s_zoom > FREECAM_ZOOM_MAX) s_zoom = FREECAM_ZOOM_MAX;
}
