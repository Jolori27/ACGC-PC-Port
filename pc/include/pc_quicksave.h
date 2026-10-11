/* pc_quicksave.h - Quick Save (pause menu) and Quick Load (title menu) */
#ifndef PC_QUICKSAVE_H
#define PC_QUICKSAVE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;
struct game_play_s;
struct door_data_s;

/* The play state currently running, or NULL on any other screen. */
extern struct game_play_s* g_pc_play_now;

enum {
    PC_QUICKSAVE_OK,       /* the save has started (fade out, write, reload) */
    PC_QUICKSAVE_NOT_HERE, /* not in a state where saving is safe */
};

/* TRUE when a quick save would be accepted right now. */
int pc_quicksave_allowed(void);
/* Start a quick save: a scene change back into the town at the player's
 * spot, with the save written while the town is torn down. */
int pc_quicksave_request(void);
/* m_play.c play_cleanup: writes the pending quick save. */
void pc_quicksave_on_play_cleanup(void);
/* pc_m_card.c: any other save deletes the recorded position. */
void pc_quicksave_invalidate(void);

/* Title menu: re-check whether the quick save on disk can be loaded. */
int pc_quickload_refresh(void);
int pc_quickload_available(void);
/* Title menu: the player chose Quick Load. */
void pc_quickload_request(void);
void pc_quickload_cancel(void);
int pc_quickload_requested(void);
/* From the choice on the title until the player is in town. */
int pc_quickload_in_progress(void);
/* Player select: start loading; returns the saved player number, or -1. */
int pc_quickload_begin(void);
/* Player select: replace the house-door start with the saved spot when that
 * is safe (no start-up demo due, the spot unchanged). */
void pc_quickload_door_data(struct door_data_s* door);
/* Player select: the scene change into town has been accepted. */
void pc_quickload_done(void);

/* m_scene.c: exact facing for the player spawned by a quick save/load. */
int pc_spawn_angle_take(s16* angle);

/* A short message at the top of the screen for a few seconds. */
void pc_quicksave_toast(const char* text);
/* graph.c: draws the message after the frame's own drawing. */
void pc_quicksave_draw(struct game_s* game);

#ifdef __cplusplus
}
#endif

#endif /* PC_QUICKSAVE_H */
