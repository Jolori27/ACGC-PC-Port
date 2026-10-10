/* pc_minimap.c - town minimap HUD.
 *
 * Reuses the in-game map screen's acre art (rivers, cliffs, beach and the
 * buildings are drawn into each acre tile, see m_map_ovl.c) and shows it in
 * a screen corner while walking around town, with villager house roofs (the
 * only thing the map screen draws over the art), the player's own house, a
 * dot for the player and optional dots for the NPCs around. Toggled with
 * the Minimap key (keybindings.ini, Tab / LB by default); size, corner and
 * NPC dots live in settings.ini and the Map settings tab.
 *
 * Everything is emitted as texture rectangles into a sub-list called from
 * FONT_DISP (same layer as the clock), so it sits above the 3D world and
 * under fades, the pause menu and the menu backdrop capture. The widescreen
 * stretch tag maps 0..320 to the whole window so the minimap hugs the real
 * window corner; widths are scaled by the aspect factor to keep tiles
 * square. */
#include "pc_minimap.h"
#include "pc_platform.h"
#include "pc_settings.h"
#include "pc_pause_menu.h"
#include "pc_settings_menu.h"
#include "pc_typing.h"

#include "m_play.h"
#include "m_map_ovl.h"
#include "m_common_data.h"
#include "m_field_info.h"
#include "m_event.h"
#include "m_demo.h"
#include "m_msg.h"
#include "m_submenu.h"
#include "m_player_lib.h"
#include "m_house.h"
#include "m_name_table.h"
#include "m_npc.h"
#include "m_private.h"
#include "m_scene_table.h"
#include "m_rcp.h"
#include "ac_npc.h"
#include "gfxalloc.h"
#include "graph.h"

#include <math.h>

extern u8 kan_win_yane_tex[];
extern u8 kan_win_ply_tex[];

#define MM_TEX_FULL_ALPHA 235 /* overall opacity, out of 255 */
#define MM_REFRESH_FRAMES 30.0f

/* The acre art fills the top-left 22x22 texels of each 32x32 tile (the map
 * screen places tiles 22 units apart); the rest is transparent, and the
 * outermost texels draw a light grid line between acres. Skip one texel on
 * each side (bilinear sampling adds half a texel, so texel centres 1.5 to
 * 20.5 are used) and neighbouring acres join seamlessly. */
#define MM_ACRE_TEXEL_START 1.0f
#define MM_ACRE_TEXELS      19.0f

static const mMP_Ovl_c* s_map = NULL;
static f32 s_alpha = 0.0f; /* 0..1, fades on toggle */
static f32 s_refresh_timer = 0.0f;

/* 16x16 I4 disc for the player and NPC dots, built on first use. */
static u8 s_dot_tex[16 * 16 / 2] ATTRIBUTE_ALIGN(32);
static int s_dot_ready = FALSE;

/* Session toggle only: rewriting settings.ini on a gameplay key would
 * clobber edits made while the game runs. The Map tab's Minimap setting
 * (saved on Apply) is the default at launch. */
void pc_minimap_toggle(void) {
    g_pc_settings.minimap = !g_pc_settings.minimap;
}

int pc_minimap_toggle_allowed(void) {
    return !pc_settings_menu_active() && !g_pc_typing_mode && !g_pc_editor_active &&
           !g_pc_nes_active && !g_pc_title_main_menu_visible && !g_pc_model_viewer;
}

/* Outdoors in the town field (not the island, title demo or intro) with
 * the player present: the map data and player position are meaningful. */
static int mm_in_town(GAME_PLAY* play) {
    if (play->scene_id != SCENE_FG || !mFI_CheckFieldData() || mFI_GET_TYPE(mFI_GetFieldId()) != mFI_FIELD_FG) {
        return FALSE;
    }
    if (mEv_CheckTitleDemo() != mEv_TITLEDEMO_NONE || mEv_CheckFirstIntro()) {
        return FALSE;
    }
    if (!Common_Get(player_actor_exists) || GET_PLAYER_ACTOR(play) == NULL || mFI_CheckInIsland()) {
        return FALSE;
    }
    /* Same rule as the X-button map: the player must own a map of this town. */
    return Common_Get(map_flag) != FALSE;
}

/* Something else owns the screen right now (menus, dialogs, cutscenes,
 * scene fades): hide at once, the menu draws below FONT. */
static int mm_screen_busy(GAME_PLAY* play) {
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);

    return g_pc_paused || play->submenu.process_status != mSM_PROCESS_WAIT || play->submenu.mode != mSM_MODE_IDLE ||
           play->fb_fade_type != FADE_TYPE_NONE || play->fb_wipe_mode != WIPE_MODE_NONE ||
           play->fb_mode != FBDEMO_MODE_NONE || mDemo_CheckDemo() ||
           !mMsg_Check_main_hide(mMsg_Get_base_window_p()) ||
           player->address_able_display == mPlayer_ADDRESSABLE_FALSE_TALKING;
}

/* GX I4 texture: 8x8 texel blocks, two texels per byte (high nibble
 * first). Full intensity inside radius 6.5, fading out by 7.5. */
static void mm_make_dot_tex(void) {
    int x;
    int y;

    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            f32 dx = (f32)x + 0.5f - 8.0f;
            f32 dy = (f32)y + 0.5f - 8.0f;
            f32 cov = 7.5f - sqrtf(dx * dx + dy * dy);
            int level;
            int texel = (y % 8) * 8 + (x % 8);
            u8* p = &s_dot_tex[((y / 8) * 2 + (x / 8)) * 32 + texel / 2];

            if (cov < 0.0f) cov = 0.0f;
            if (cov > 1.0f) cov = 1.0f;
            level = (int)(cov * 15.0f + 0.5f);

            if (texel & 1) {
                *p = (u8)((*p & 0xF0) | level);
            } else {
                *p = (u8)((*p & 0x0F) | (level << 4));
            }
        }
    }
}

static int mm_q(f32 v) {
    return (int)(v * 4.0f + 0.5f);
}

/* Texture rectangle with edges in quarter units of the 320x240 stretch
 * space, showing tex_w x tex_h texels from the top-left of the bound
 * texture. The step comes from the emitted (rounded) size, so the last
 * column never samples past tex_w. */
static Gfx* mm_texrect_q(Gfx* gfx, int qx0, int qy0, int qx1, int qy1, f32 tex_s, f32 tex_t, f32 tex_w,
                         f32 tex_h) {
    int dsdx = (int)(tex_w * 4096.0f / (f32)(qx1 - qx0));
    int dtdy = (int)(tex_h * 4096.0f / (f32)(qy1 - qy0));

    /* s/t are S10.5 texel coordinates. */
    return gfx_gSPScisTextureRectangle1(gfx, qx0, qy0, qx1, qy1, G_TX_RENDERTILE, (int)(tex_s * 32.0f),
                                        (int)(tex_t * 32.0f), dsdx, dtdy);
}

static Gfx* mm_texrect(Gfx* gfx, f32 x0, f32 y0, f32 x1, f32 y1, f32 tex_s, f32 tex_t, f32 tex_w, f32 tex_h) {
    return mm_texrect_q(gfx, mm_q(x0), mm_q(y0), mm_q(x1), mm_q(y1), tex_s, tex_t, tex_w, tex_h);
}

static Gfx* mm_mode(Gfx* gfx, u32 tlut) {
    gDPPipeSync(gfx++);
    gDPSetOtherMode(gfx++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | tlut | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    return gfx;
}

static Gfx* mm_fill(Gfx* gfx, f32 x0, f32 y0, f32 x1, f32 y1, int r, int g, int b, int a) {
    gfx = mm_mode(gfx, G_TT_NONE);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    return gfx_gSPScisTextureRectangle1(gfx, mm_q(x0), mm_q(y0), mm_q(x1), mm_q(y1), G_TX_RENDERTILE, 0, 0, 0, 0);
}

/* 16x16 map icon. I4 icons are a PRIM-coloured shape; IA8 icons blend
 * from ENV (dark texels) to PRIM (bright texels), as on the map screen. */
typedef struct {
    u8* tex;
    int ia8;
    u8 prim[3];
    u8 env[3];
} MMIcon;

static Gfx* mm_icon(Gfx* gfx, const MMIcon* icon, f32 cx, f32 cy, f32 w, f32 h, int alpha) {
    gDPPipeSync(gfx++);
    if (icon->ia8) {
        gDPSetCombineLERP(gfx++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                          ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
        gDPSetEnvColor(gfx++, icon->env[0], icon->env[1], icon->env[2], 255);
        gDPSetTextureImage_Dolphin(gfx++, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 16, icon->tex);
    } else {
        gDPSetCombineLERP(gfx++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0,
                          PRIMITIVE, 0);
        gDPSetTextureImage_Dolphin(gfx++, G_IM_FMT_I, G_IM_SIZ_4b, 16, 16, icon->tex);
    }
    gDPSetPrimColor(gfx++, 0, 255, icon->prim[0], icon->prim[1], icon->prim[2], alpha);
    gDPSetTile_Dolphin(gfx++, G_DOLPHIN_TLUT_DEFAULT_MODE, G_TX_RENDERTILE, 0, GX_CLAMP, GX_CLAMP, 0, 0);

    /* Round the centre and the half size once, so a moving icon keeps its
     * size and stacked icons (dot ring + fill) stay concentric. */
    {
        int qcx = mm_q(cx);
        int qcy = mm_q(cy);
        int hw = (int)(w * 2.0f + 0.5f);
        int hh = (int)(h * 2.0f + 0.5f);

        if (hw < 1) hw = 1;
        if (hh < 1) hh = 1;
        return mm_texrect_q(gfx, qcx - hw, qcy - hh, qcx + hw, qcy + hh, 0.0f, 0.0f, 16.0f, 16.0f);
    }
}

/* Round dot with an outline ring, d = inner diameter in screen units. */
static Gfx* mm_dot(Gfx* gfx, f32 cx, f32 cy, f32 d, f32 ring, f32 xs, const u8* fill, const u8* edge, int alpha) {
    MMIcon dot;

    dot.tex = s_dot_tex;
    dot.ia8 = FALSE;
    dot.env[0] = dot.env[1] = dot.env[2] = 0;

    dot.prim[0] = edge[0];
    dot.prim[1] = edge[1];
    dot.prim[2] = edge[2];
    gfx = mm_icon(gfx, &dot, cx, cy, (d + ring) * xs, d + ring, alpha);

    dot.prim[0] = fill[0];
    dot.prim[1] = fill[1];
    dot.prim[2] = fill[2];
    return mm_icon(gfx, &dot, cx, cy, d * xs, d, alpha);
}

/* Index of the resident behind an NPC actor (event villagers included,
 * see mNpc_SetNpcinfo), or -1 for other NPCs. */
static int mm_villager_idx(NPC_ACTOR* npc_actor) {
    mNpc_NpcList_c* list = npc_actor->npc_info.list;
    mNpc_NpcList_c* first = Common_GetPointer(npclist[0]);

    if (list != NULL && list >= first && list < first + ANIMAL_NUM_MAX) {
        return (int)(list - first);
    }
    return -1;
}

/* World position -> map units (acres from the top-left of row A). */
static int mm_world_to_map(const xyz_t* pos, f32* u, f32* v) {
    *u = pos->x / mFI_BK_WORLDSIZE_X_F - 1.0f;
    *v = pos->z / mFI_BK_WORLDSIZE_Z_F - 1.0f;
    return *u >= 0.0f && *u <= (f32)FG_BLOCK_X_NUM && *v >= 0.0f && *v <= (f32)FG_BLOCK_Z_NUM;
}

void pc_minimap_draw(GAME_PLAY* play) {
    static const f32 tile_sizes[3] = { 12.0f, 16.0f, 21.0f };
    static const s16 home_x[PLAYER_NUM] = { 2128, 2352, 2128, 2352 };
    static const s16 home_z[PLAYER_NUM] = { 1488, 1488, 1768, 1768 };
    static const MMIcon my_home_icon = { kan_win_ply_tex, TRUE, { 230, 90, 50 }, { 255, 235, 120 } };
    /* Villager roof colours by cliff layer (kan_win_npc2T_*_model). */
    static const u8 roof_prim[3][3] = { { 90, 90, 225 }, { 145, 70, 205 }, { 170, 115, 20 } };
    static const u8 player_fill[3] = { 255, 70, 30 };
    static const u8 villager_fill[3] = { 70, 170, 255 };
    static const u8 special_fill[3] = { 255, 205, 40 };
    static const u8 white[3] = { 255, 255, 255 };
    static const u8 dark[3] = { 40, 40, 50 };
    GRAPH* graph;
    PLAYER_ACTOR* player;
    ACTOR* npc;
    Gfx* poly;
    Gfx* gfx;
    f32 dt;
    f32 aspect;
    f32 xs;
    f32 tile_h;
    f32 tile_w;
    f32 x0;
    f32 y0;
    f32 map_w;
    f32 map_h;
    f32 pad;
    f32 icon;
    f32 u;
    f32 v;
    int alpha;
    int i;

    if (play == NULL || !mm_in_town(play)) {
        s_alpha = 0.0f;
        s_map = NULL;
        return;
    }

    if (mm_screen_busy(play)) {
        s_alpha = 0.0f;
        return;
    }

    graph = play->game.graph;
    dt = (f32)graph->dt_num_60fps_frames;
    if (g_pc_settings.minimap) {
        s_alpha += dt * (1.0f / 8.0f);
        if (s_alpha > 1.0f) s_alpha = 1.0f;
    } else {
        s_alpha -= dt * (1.0f / 6.0f);
        if (s_alpha < 0.0f) s_alpha = 0.0f;
    }
    if (s_alpha <= 0.0f) {
        s_map = NULL;
        return;
    }

    /* The acre/house data only changes with the town (travel, new
     * villagers, bridge): rebuild when shown and every half second. */
    s_refresh_timer -= dt;
    if (s_map == NULL || s_refresh_timer <= 0.0f) {
        s_map = mMP_pc_minimap_build();
        s_refresh_timer = MM_REFRESH_FRAMES;
    }

    if (!s_dot_ready) {
        mm_make_dot_tex();
        s_dot_ready = TRUE;
    }

    /* Layout in 320x240 stretch space: one unit of x is xs screen units. */
    aspect = (g_pc_window_h > 0) ? (f32)g_pc_window_w / (f32)g_pc_window_h : (4.0f / 3.0f);
    xs = (aspect > (4.0f / 3.0f) + 0.01f) ? (4.0f / 3.0f) / aspect : 1.0f;
    tile_h = tile_sizes[(u32)g_pc_settings.minimap_size < 3 ? g_pc_settings.minimap_size : 1];
    tile_w = tile_h * xs;
    map_w = tile_w * FG_BLOCK_X_NUM;
    map_h = tile_h * FG_BLOCK_Z_NUM;
    pad = 3.0f;
    y0 = 10.0f;
    x0 = g_pc_settings.minimap_corner ? 10.0f * xs + pad * xs : 320.0f - 10.0f * xs - pad * xs - map_w;
    icon = tile_h * 0.55f;
    alpha = (int)(s_alpha * MM_TEX_FULL_ALPHA);
    player = GET_PLAYER_ACTOR(play);

    OPEN_DISP(graph);
    poly = NOW_POLY_OPA_DISP;
    gfx = gfxopen(poly);
    gSPDisplayList(NOW_FONT_DISP++, gfx);

    gDPNoOpTag(gfx++, PC_NOOP_WIDESCREEN_STRETCH);

    /* Frame: soft shadow, then a cream card like the map screen. */
    gfx = mm_fill(gfx, x0 - pad * xs + 1.5f * xs, y0 - pad + 1.5f, x0 + map_w + pad * xs + 1.5f * xs,
                  y0 + map_h + pad + 1.5f, 0, 0, 0, (int)(s_alpha * 90.0f));
    gfx = mm_fill(gfx, x0 - pad * xs, y0 - pad, x0 + map_w + pad * xs, y0 + map_h + pad, 245, 235, 205, alpha);

    /* Acre tiles: CI4 32x32 with the map screen's palettes. */
    gfx = mm_mode(gfx, G_TT_RGBA16);
    gDPSetCombineLERP(gfx++, 0, 0, 0, TEXEL0, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, TEXEL0, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetPrimColor(gfx++, 0, 255, 255, 255, 255, alpha);
    for (i = 0; i < FG_BLOCK_TOTAL_NUM; i++) {
        int fx = i % FG_BLOCK_X_NUM;
        int fz = i / FG_BLOCK_X_NUM;

        if (s_map->map_texture[i] == NULL) {
            continue;
        }

        gDPPipeSync(gfx++);
        gDPLoadTLUT_Dolphin(gfx++, 15, 16, 1, mMP_pc_minimap_palette(s_map->map_pal[i]));
        gDPSetTextureImage_Dolphin(gfx++, G_IM_FMT_CI, G_IM_SIZ_4b, 32, 32, s_map->map_texture[i]);
        gDPSetTile_Dolphin(gfx++, G_DOLPHIN_TLUT_DEFAULT_MODE, G_TX_RENDERTILE, 15, GX_CLAMP, GX_CLAMP, 0, 0);
        gfx = mm_texrect(gfx, x0 + fx * tile_w, y0 + fz * tile_h, x0 + (fx + 1) * tile_w, y0 + (fz + 1) * tile_h,
                         MM_ACRE_TEXEL_START, MM_ACRE_TEXEL_START, MM_ACRE_TEXELS, MM_ACRE_TEXELS);
    }

    /* The acre art already shows every building (each building acre has its
     * own tile, the dump included), so like the map screen only villager
     * houses go on top. The house sits one unit above home_info.ut_z. */
    gfx = mm_mode(gfx, G_TT_NONE);
    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        Animal_c* animal = Save_GetPointer(animals[i]);
        Anmhome_c* home = &animal->home_info;
        MMIcon roof;
        int layer;

        if (mNpc_CheckFreeAnimalInfo(animal) || home->block_x < 1 || home->block_x > FG_BLOCK_X_NUM ||
            home->block_z < 1 || home->block_z > FG_BLOCK_Z_NUM) {
            continue;
        }

        layer = s_map->animal_info[i].house_layer;
        if (layer > 2) layer = 2;
        roof.tex = kan_win_yane_tex;
        roof.ia8 = TRUE;
        roof.prim[0] = roof_prim[layer][0];
        roof.prim[1] = roof_prim[layer][1];
        roof.prim[2] = roof_prim[layer][2];
        roof.env[0] = roof.env[1] = roof.env[2] = 225;

        u = (f32)(home->block_x - 1) + ((f32)home->ut_x + 0.5f) / (f32)UT_X_NUM;
        v = (f32)(home->block_z - 1) + ((f32)home->ut_z - 0.5f) / (f32)UT_Z_NUM;
        gfx = mm_icon(gfx, &roof, x0 + u * tile_w, y0 + v * tile_h, icon * xs, icon, alpha);
    }

    /* The player's own house (the four houses are in the acre art). */
    if (Common_Get(player_no) < PLAYER_NUM) {
        i = mHS_get_arrange_idx(Common_Get(player_no));
        u = (f32)home_x[i] / mFI_BK_WORLDSIZE_X_F - 1.0f;
        v = ((f32)home_z[i] - mFI_UT_WORLDSIZE_Z_F) / mFI_BK_WORLDSIZE_Z_F - 1.0f;
        gfx = mm_icon(gfx, &my_home_icon, x0 + u * tile_w, y0 + v * tile_h, icon * xs, icon, alpha);
    }

    /* NPC dots. The game only keeps the NPCs near the player loaded and
     * running; those get a live dot. Villagers further away are frozen
     * where they were last unloaded (mNpc_RenewalSetNpc stores that in
     * npclist[].position), shown as a faded dot until they load again. */
    if (g_pc_settings.minimap_npcs) {
        u32 loaded = 0;

        /* Every NPC-part actor is an NPC_ACTOR (constructed once ct_proc is
         * cleared). Note which residents are loaded, hidden ones included
         * (villagers at home or asleep stay loaded, hidden on their house). */
        for (npc = play->actor_info.list[ACTOR_PART_NPC].actor; npc != NULL; npc = npc->next_actor) {
            int idx;

            if (npc->ct_proc == NULL && (idx = mm_villager_idx((NPC_ACTOR*)npc)) >= 0) {
                loaded |= 1u << idx;
            }
        }

        /* Faded dots first, so live ones stay on top. */
        for (i = 0; i < ANIMAL_NUM_MAX; i++) {
            if (((loaded >> i) & 1) || mNpc_CheckFreeAnimalInfo(Save_GetPointer(animals[i])) ||
                !mm_world_to_map(Common_GetPointer(npclist[i].position), &u, &v)) {
                continue;
            }

            gfx = mm_dot(gfx, x0 + u * tile_w, y0 + v * tile_h, tile_h * 0.2f, tile_h * 0.09f, xs, villager_fill,
                         dark, (int)(alpha * 0.45f));
        }

        for (npc = play->actor_info.list[ACTOR_PART_NPC].actor; npc != NULL; npc = npc->next_actor) {
            NPC_ACTOR* npc_actor = (NPC_ACTOR*)npc;

            if (npc->ct_proc != NULL || npc_actor->condition_info.hide_flg ||
                (npc->state_bitfield & ACTOR_STATE_INVISIBLE) || !mm_world_to_map(&npc->world.position, &u, &v)) {
                continue;
            }

            gfx = mm_dot(gfx, x0 + u * tile_w, y0 + v * tile_h, tile_h * 0.2f, tile_h * 0.09f, xs,
                         mm_villager_idx(npc_actor) >= 0 ? villager_fill : special_fill, dark, alpha);
        }
    }

    /* The player: red dot with a white ring. */
    mm_world_to_map(&player->actor_class.world.position, &u, &v);
    if (u < 0.0f) u = 0.0f;
    if (u > (f32)FG_BLOCK_X_NUM) u = (f32)FG_BLOCK_X_NUM;
    if (v < 0.0f) v = 0.0f;
    if (v > (f32)FG_BLOCK_Z_NUM) v = (f32)FG_BLOCK_Z_NUM;
    gfx = mm_dot(gfx, x0 + u * tile_w, y0 + v * tile_h, tile_h * 0.26f, tile_h * 0.11f, xs, player_fill, white,
                 (int)(s_alpha * 255.0f));

    gDPPipeSync(gfx++);
    gDPNoOpTag(gfx++, PC_NOOP_WIDESCREEN_STRETCH_OFF);
    gSPEndDisplayList(gfx++);

    gfxclose(poly, gfx);
    SET_POLY_OPA_DISP(gfx);
    CLOSE_DISP(graph);
}
