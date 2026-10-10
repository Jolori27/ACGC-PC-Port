/* pc_minimap.h - town minimap HUD (toggled with the Minimap key) */
#ifndef PC_MINIMAP_H
#define PC_MINIMAP_H

#ifdef __cplusplus
extern "C" {
#endif

struct game_play_s;

/* Flip the minimap on/off for this session (the settings.ini value,
 * set from the Map tab, is the default at launch). */
void pc_minimap_toggle(void);

/* FALSE while a text field, the settings menu or a non-game screen owns
 * the keyboard/pad, so the Minimap key is left alone there. */
int pc_minimap_toggle_allowed(void);

/* Called from Game_play_draw after the clock HUD. Draws nothing unless the
 * player is outdoors in town and nothing else (menu, dialog, cutscene,
 * fade) is on screen. */
void pc_minimap_draw(struct game_play_s* play);

#ifdef __cplusplus
}
#endif

#endif /* PC_MINIMAP_H */
