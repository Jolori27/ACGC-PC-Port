/* pc_quicksave.c - save the game from the pause menu
 *
 * Uses the same write path as the Gyroid's "save and continue": the save
 * is synchronous on PC, so the result is known immediately. Saving is only
 * allowed outdoors in town while the player is simply standing or moving,
 * with no dialogue, menu, cutscene or transition in progress: that is the
 * state the Gyroid saves from, so the loaded game restarts cleanly at the
 * player's house. */
#include "pc_quicksave.h"
#include "pc_platform.h"
#include "pc_pause_menu.h"

#include "m_play.h"
#include "m_common_data.h"
#include "m_field_info.h"
#include "m_event.h"
#include "m_demo.h"
#include "m_msg.h"
#include "m_submenu.h"
#include "m_player_lib.h"
#include "m_card.h"
#include "m_scene_table.h"

#include <stdio.h>

struct game_play_s* g_pc_play_now;

extern int pc_save_is_ready(void);

/* why is the quick save refused: for the log only */
static const char* s_refuse_reason = "";

static int quicksave_state_ok(GAME_PLAY* play) {
    PLAYER_ACTOR* player;
    int main_index;

    s_refuse_reason = "";
    if (play == NULL) {
        s_refuse_reason = "not in play";
        return FALSE;
    }
    if (!pc_save_is_ready()) {
        s_refuse_reason = "save not ready";
        return FALSE;
    }
    if (play->scene_id != SCENE_FG || !mFI_CheckFieldData() || mFI_GET_TYPE(mFI_GetFieldId()) != mFI_FIELD_FG ||
        mFI_CheckInIsland()) {
        s_refuse_reason = "not outdoors in town";
        return FALSE;
    }
    if (mEv_CheckTitleDemo() != mEv_TITLEDEMO_NONE || mEv_CheckFirstIntro()) {
        s_refuse_reason = "title demo / first intro";
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

int pc_quicksave_do(void) {
    int chan = 0;
    int res;

    if (!quicksave_state_ok(g_pc_play_now)) {
        printf("[QUICKSAVE] refused: %s\n", s_refuse_reason);
        return PC_QUICKSAVE_NOT_HERE;
    }

    /* 0 = the Gyroid's "save and continue" mode. */
    res = mCD_SaveHome_bg(0, &chan);
    if (res != mCD_TRANS_ERR_NONE) {
        printf("[QUICKSAVE] failed (%d)\n", res);
        return PC_QUICKSAVE_FAILED;
    }

    printf("[QUICKSAVE] saved\n");
    return PC_QUICKSAVE_OK;
}
