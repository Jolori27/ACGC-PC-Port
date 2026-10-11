/* pc_quicksave.c - Quick Save (pause menu) and Quick Load (title menu)
 *
 * QUICK SAVE. While the town is running, every building, mailbox and Gyroid
 * replaces its own tile in the save's town map with a placeholder, and the
 * villagers, the event manager and the quest manager keep part of their
 * state in their actors. The game only writes all of that back when the
 * town is torn down, which is why the original game never saves from
 * inside the town. (Writing the live map is what erased the houses in the
 * first version of this feature.) So a quick save is a real scene change:
 * the screen fades out, the town is torn down (every actor's save proc runs
 * and every building's real id goes back into the map), the save is written
 * at the end of that teardown, and the town is loaded again with the player
 * standing exactly where they were.
 *
 * The file is written like the Gyroid's "save and continue" (Wisp pieces and
 * the money rock are left alone) but with the reset code cleared, so
 * quitting right after a quick save does not bring Mr. Resetti.
 *
 * QUICK LOAD. Next to the save, save/quicksave.dat records where the player
 * stood. It is tied to the save by the town id, the copy-protect value and
 * the save's hard time, which the game re-rolls on every write, and every
 * other kind of save deletes it. "Quick Load" on the title menu runs the
 * normal Continue path for that player, skips the spotlight villager's
 * dialogue and puts the player back on that spot. A quick-load session does
 * not write the save at start-up, so the same quick save can be loaded again
 * until the player saves. */
#include "pc_quicksave.h"
#include "pc_platform.h"
#include "pc_pause_menu.h"
#include "pc_menu_util.h"

#include "m_play.h"
#include "m_common_data.h"
#include "m_field_info.h"
#include "m_field_make.h"
#include "m_event.h"
#include "m_demo.h"
#include "m_msg.h"
#include "m_submenu.h"
#include "m_player_lib.h"
#include "m_card.h"
#include "m_scene.h"
#include "m_scene_table.h"
#include "m_actor.h"
#include "m_name_table.h"
#include "m_font.h"
#include "graph.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

struct game_play_s* g_pc_play_now;

/* pc_m_card.c */
extern int pc_save_is_ready(void);
extern int pc_save_quick_write(void);
extern int pc_save_peek_token(u16* land_id, u16* copy_protect, OSTime* hard_time, int player_no, int* exists);

#define PC_QS_FILE "save/quicksave.dat"
#define PC_QS_MAGIC "ACQSAVE1"
#define PC_QS_VERSION 1

typedef struct {
    char magic[8];
    u32 version;
    u16 land_id;
    u16 copy_protect;
    OSTime hard_time;
    s32 player_no;
    f32 pos_x;
    f32 pos_y;
    f32 pos_z;
    s16 angle;
    u16 cell_item; /* town map item under the player when saved */
    u8 land_name[8];
    u8 player_name[PLAYER_NAME_LEN];
    s64 unix_time;
} PcQuickSaveRec;

static int s_save_pending;          /* scene change started, write at the end of teardown */
static PcQuickSaveRec s_save_rec;   /* where the player stood */

static int s_load_available;        /* title: a valid quick save exists */
static int s_load_state;            /* 0 none, 1 chosen on the title, 2 loading in the player select */
static PcQuickSaveRec s_load_rec;

static int s_angle_override;        /* exact facing for the next player spawn */
static s16 s_angle_value;

/* why is the quick save refused: for the log only */
static const char* s_refuse_reason = "";

/* ---- helpers ---- */

/* Town map item at a world position, read straight from the save. Returns
 * RSV_NO when the position is outside the town map. */
static mActor_name_t qs_cell_item(f32 x, f32 z) {
    int bx = (int)(x / mFI_BK_WORLDSIZE_X_F);
    int bz = (int)(z / mFI_BK_WORLDSIZE_Z_F);
    int ux;
    int uz;

    if (x < 0.0f || z < 0.0f || bx < 1 || bx > FG_BLOCK_X_NUM || bz < 1 || bz > FG_BLOCK_Z_NUM) {
        return RSV_NO;
    }

    ux = (int)((x - (f32)bx * mFI_BK_WORLDSIZE_X_F) / mFI_UT_WORLDSIZE_X_F);
    uz = (int)((z - (f32)bz * mFI_BK_WORLDSIZE_Z_F) / mFI_UT_WORLDSIZE_Z_F);
    if (ux < 0 || ux >= UT_X_NUM || uz < 0 || uz >= UT_Z_NUM) {
        return RSV_NO;
    }

    return Save_Get(fg[bz - 1][bx - 1]).items[uz][ux];
}

static s16 qs_round(f32 v) {
    return (s16)(v < 0.0f ? v - 0.5f : v + 0.5f);
}

static void qs_remove_file(void) {
    remove(PC_QS_FILE);
}

static int qs_read_file(PcQuickSaveRec* rec) {
    FILE* f = fopen(PC_QS_FILE, "rb");
    int ok;

    if (f == NULL) {
        return FALSE;
    }
    ok = fread(rec, sizeof(*rec), 1, f) == 1;
    fclose(f);

    return ok && memcmp(rec->magic, PC_QS_MAGIC, 8) == 0 && rec->version == PC_QS_VERSION &&
           rec->player_no >= 0 && rec->player_no < PLAYER_NUM;
}

static int qs_write_file(const PcQuickSaveRec* rec) {
    FILE* f = fopen(PC_QS_FILE ".tmp", "wb");
    int ok;

    if (f == NULL) {
        return FALSE;
    }
    ok = fwrite(rec, sizeof(*rec), 1, f) == 1;
    ok &= fclose(f) == 0;
    if (!ok) {
        remove(PC_QS_FILE ".tmp");
        return FALSE;
    }
    remove(PC_QS_FILE);
    return rename(PC_QS_FILE ".tmp", PC_QS_FILE) == 0;
}

/* ---- quick save ---- */

static int quicksave_state_ok(GAME_PLAY* play) {
    PLAYER_ACTOR* player;
    int main_index;

    s_refuse_reason = "";
    if (play == NULL) {
        s_refuse_reason = "not in play";
        return FALSE;
    }
    if (s_save_pending) {
        s_refuse_reason = "already saving";
        return FALSE;
    }
    if (!pc_save_is_ready() || mCD_GetThisLandSlotNo() == mCD_SLOT_B) {
        s_refuse_reason = "save not ready / not the home town";
        return FALSE;
    }
    if (play->scene_id != SCENE_FG || !mFI_CheckFieldData() || mFI_GET_TYPE(mFI_GetFieldId()) != mFI_FIELD_FG ||
        mFI_CheckInIsland()) {
        s_refuse_reason = "not outdoors in town";
        return FALSE;
    }
    if (mEv_CheckTitleDemo() != mEv_TITLEDEMO_NONE || mEv_CheckFirstIntro() || mEv_CheckFirstJob()) {
        s_refuse_reason = "title demo / intro / first job";
        return FALSE;
    }
    if (!Common_Get(player_actor_exists) || Common_Get(player_no) >= PLAYER_NUM) {
        s_refuse_reason = "no player";
        return FALSE;
    }
    player = GET_PLAYER_ACTOR(play);
    if (player == NULL) {
        s_refuse_reason = "no player actor";
        return FALSE;
    }
    if (play->submenu.process_status != mSM_PROCESS_WAIT || play->submenu.mode != mSM_MODE_IDLE ||
        play->fb_fade_type != FADE_TYPE_NONE || play->fb_wipe_mode != WIPE_MODE_NONE ||
        play->fb_mode != FBDEMO_MODE_NONE || mDemo_CheckDemo() || !mMsg_Check_main_hide(mMsg_Get_base_window_p()) ||
        player->address_able_display == mPlayer_ADDRESSABLE_FALSE_TALKING) {
        s_refuse_reason = "menu, dialogue, cutscene or transition in progress";
        return FALSE;
    }

    main_index = player->now_main_index;
    if (main_index != mPlayer_INDEX_WAIT && main_index != mPlayer_INDEX_WALK && main_index != mPlayer_INDEX_RUN &&
        main_index != mPlayer_INDEX_DASH) {
        s_refuse_reason = "player busy";
        return FALSE;
    }

    return TRUE;
}

int pc_quicksave_allowed(void) {
    return quicksave_state_ok(g_pc_play_now);
}

int pc_quicksave_request(void) {
    GAME_PLAY* play = g_pc_play_now;
    PLAYER_ACTOR* player;
    Door_data_c door;
    xyz_t pos;
    s16 angle;

    if (!quicksave_state_ok(play)) {
        printf("[QUICKSAVE] refused: %s\n", s_refuse_reason);
        return PC_QUICKSAVE_NOT_HERE;
    }

    player = GET_PLAYER_ACTOR(play);
    pos = player->actor_class.world.position;
    angle = player->actor_class.shape_info.rotation.y;

    /* Same shape as the NES console's return-to-town door (return_emu_game):
     * appear standing at a world position, no door. */
    memset(&door, 0, sizeof(door));
    door.next_scene_id = SCENE_FG;
    door.exit_orientation = (u8)(((u16)(angle + 0x1000)) >> 13);
    door.exit_type = 0;
    door.extra_data = 0;
    door.exit_position.x = qs_round(pos.x);
    door.exit_position.y = qs_round(pos.y);
    door.exit_position.z = qs_round(pos.z);
    door.door_actor_name = EMPTY_NO;
    door.wipe_type = WIPE_TYPE_FADE_BLACK;

    if (goto_other_scene(play, &door, FALSE) != 1) {
        s_refuse_reason = "scene change refused";
        printf("[QUICKSAVE] refused: %s\n", s_refuse_reason);
        return PC_QUICKSAVE_NOT_HERE;
    }

    memset(&s_save_rec, 0, sizeof(s_save_rec));
    s_save_rec.player_no = Common_Get(player_no);
    s_save_rec.pos_x = (f32)door.exit_position.x;
    s_save_rec.pos_y = (f32)door.exit_position.y;
    s_save_rec.pos_z = (f32)door.exit_position.z;
    s_save_rec.angle = angle;
    s_save_pending = TRUE;

    s_angle_override = TRUE;
    s_angle_value = angle;

    printf("[QUICKSAVE] scene change started at (%d, %d, %d)\n", door.exit_position.x, door.exit_position.y,
           door.exit_position.z);
    return PC_QUICKSAVE_OK;
}

void pc_quicksave_on_play_cleanup(void) {
    PcQuickSaveRec* rec = &s_save_rec;

    if (!s_save_pending) {
        return;
    }
    s_save_pending = FALSE;

    if (!pc_save_quick_write()) {
        printf("[QUICKSAVE] write failed\n");
        pc_quicksave_toast("Quick save failed!");
        return;
    }

    memcpy(rec->magic, PC_QS_MAGIC, 8);
    rec->version = PC_QS_VERSION;
    rec->land_id = Save_Get(land_info).id;
    rec->copy_protect = Save_Get(copy_protect);
    rec->hard_time = Save_Get(travel_hard_time);
    rec->cell_item = qs_cell_item(rec->pos_x, rec->pos_z);
    memcpy(rec->land_name, Save_Get(land_info).name, sizeof(rec->land_name));
    memcpy(rec->player_name, Now_Private->player_ID.player_name, sizeof(rec->player_name));
    rec->unix_time = (s64)time(NULL);

    if (!qs_write_file(rec)) {
        printf("[QUICKSAVE] saved, but the position file could not be written\n");
    } else {
        printf("[QUICKSAVE] saved (cell %04X)\n", rec->cell_item);
    }
    pc_quicksave_toast("Game saved.");
}

/* Every save other than a quick save makes the recorded position stale. */
void pc_quicksave_invalidate(void) {
    qs_remove_file();
}

/* ---- quick load ---- */

int pc_quickload_refresh(void) {
    PcQuickSaveRec rec;
    u16 land_id;
    u16 copy_protect;
    OSTime hard_time;
    int exists = FALSE;

    s_load_available = FALSE;
    if (!qs_read_file(&rec)) {
        return FALSE;
    }
    if (!pc_save_peek_token(&land_id, &copy_protect, &hard_time, rec.player_no, &exists)) {
        return FALSE;
    }
    if (land_id != rec.land_id || copy_protect != rec.copy_protect || hard_time != rec.hard_time || !exists) {
        printf("[QUICKLOAD] quicksave.dat does not match the save, ignoring it\n");
        return FALSE;
    }

    s_load_rec = rec;
    s_load_available = TRUE;
    return TRUE;
}

int pc_quickload_available(void) {
    return s_load_available;
}

void pc_quickload_request(void) {
    if (s_load_available) {
        s_load_state = 1;
    }
}

void pc_quickload_cancel(void) {
    if (s_load_state != 0) {
        printf("[QUICKLOAD] cancelled\n");
    }
    s_load_state = 0;
}

int pc_quickload_requested(void) {
    return s_load_state == 1;
}

int pc_quickload_in_progress(void) {
    return s_load_state != 0;
}

int pc_quickload_begin(void) {
    if (s_load_state != 1) {
        return -1;
    }
    s_load_state = 2;
    return s_load_rec.player_no;
}

void pc_quickload_door_data(Door_data_c* door) {
    mActor_name_t cell;

    if (s_load_state != 2) {
        return;
    }

    /* Anything that must play at the house (Mr. Resetti, the birthday or
     * present demos, the first job) keeps the normal house-door start. */
    if (Common_Get(demo_profiles[0]) != mAc_PROFILE_NUM || Common_Get(demo_profiles[1]) != mAc_PROFILE_NUM ||
        Common_Get(reset_flag) || mEv_CheckFirstJob()) {
        printf("[QUICKLOAD] a start-up demo is due, starting at the house\n");
        return;
    }

    /* A new day can grow a tree or drop a rock where the player stood. */
    cell = qs_cell_item(s_load_rec.pos_x, s_load_rec.pos_z);
    if (cell != s_load_rec.cell_item) {
        printf("[QUICKLOAD] the saved spot changed (%04X -> %04X), starting at the house\n", s_load_rec.cell_item,
               cell);
        return;
    }

    door->next_scene_id = SCENE_FG;
    door->exit_orientation = (u8)(((u16)(s_load_rec.angle + 0x1000)) >> 13);
    door->exit_type = 0;
    door->extra_data = 0;
    door->exit_position.x = qs_round(s_load_rec.pos_x);
    door->exit_position.y = qs_round(s_load_rec.pos_y);
    door->exit_position.z = qs_round(s_load_rec.pos_z);
    door->door_actor_name = EMPTY_NO;
    door->wipe_type = WIPE_TYPE_FADE_BLACK;
    Common_Set(transition.wipe_type, WIPE_TYPE_FADE_BLACK);

    s_angle_override = TRUE;
    s_angle_value = s_load_rec.angle;
}

void pc_quickload_done(void) {
    if (s_load_state == 2) {
        printf("[QUICKLOAD] loaded player %d\n", s_load_rec.player_no);
    }
    s_load_state = 0;
}

int pc_spawn_angle_take(s16* angle) {
    if (!s_angle_override) {
        return FALSE;
    }
    s_angle_override = FALSE;
    *angle = s_angle_value;
    return TRUE;
}

/* ---- on-screen confirmation, drawn after the frame (graph.c) ---- */

static const char* s_toast_text;
static Uint32 s_toast_until;

void pc_quicksave_toast_ms(const char* text, unsigned int ms) {
    s_toast_text = text;
    s_toast_until = SDL_GetTicks() + ms;
}

void pc_quicksave_toast(const char* text) {
    pc_quicksave_toast_ms(text, 3500);
}

void pc_quicksave_draw(struct game_s* game) {
    Uint32 now = SDL_GetTicks();
    int a;

    if (s_toast_text == NULL || g_pc_paused || game == NULL || game->graph == NULL) {
        return;
    }
    if ((Sint32)(s_toast_until - now) <= 0) {
        s_toast_text = NULL;
        return;
    }

    a = (int)(s_toast_until - now) < 500 ? (int)(s_toast_until - now) * 255 / 500 : 255;
    mFont_SetMatrix(game->graph, mFont_MODE_FONT);
    pc_menu_draw_centered(game, s_toast_text, 17.0f, 0, 0, 0, a * 3 / 4, 1.0f);
    pc_menu_draw_centered(game, s_toast_text, 16.0f, 255, 255, 255, a, 1.0f);
    mFont_UnSetMatrix(game->graph, mFont_MODE_FONT);
}
