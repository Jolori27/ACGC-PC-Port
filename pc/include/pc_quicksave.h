/* pc_quicksave.h - save the game from the pause menu */
#ifndef PC_QUICKSAVE_H
#define PC_QUICKSAVE_H

#ifdef __cplusplus
extern "C" {
#endif

struct game_play_s;

/* The play state currently running, or NULL on any other screen. */
extern struct game_play_s* g_pc_play_now;

enum {
    PC_QUICKSAVE_OK,       /* saved */
    PC_QUICKSAVE_NOT_HERE, /* not in a state where saving is safe */
    PC_QUICKSAVE_FAILED,   /* the write failed */
};

/* TRUE when a quick save would be accepted right now. */
int pc_quicksave_allowed(void);

/* Save now. Returns one of PC_QUICKSAVE_*. */
int pc_quicksave_do(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_QUICKSAVE_H */
