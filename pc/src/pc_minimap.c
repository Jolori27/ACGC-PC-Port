/* pc_minimap.c - town minimap HUD.
 *
 * Reuses the in-game map screen's art (acre tiles with the buildings drawn
 * in, building and house icons, see m_map_ovl.c) and shows it in a screen
 * corner while walking around town, with an arrow for the player. Toggled
 * with the Minimap key (keybindings.ini, Tab / LB by default); size and
 * corner live in settings.ini.
 *
 * Everything is emitted as texture rectangles plus one triangle, into a
 * sub-list called from FONT_DISP (same layer as the clock), so it sits
 * above the 3D world and under fades, the pause menu and the menu
 * backdrop capture. The widescreen stretch tag maps 0..320 to the whole
 * window so the minimap hugs the real window corner; widths are scaled by
 * the aspect factor to keep tiles square. */
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
#include "m_font.h"
#include "m_lib.h"
#include "gfxalloc.h"
#include "graph.h"
#include "sys_matrix.h"

extern u8 kan_win_omise_tex[];
extern u8 kan_win_kouban_tex[];
extern u8 kan_win_yubin_tex[];
extern u8 kan_win_yashiro_tex[];
extern u8 kan_win_gomi_tex[];
extern u8 kan_win_mu_tex[];
extern u8 kan_win_ta_tex[];
extern u8 kan_win_eki_tex[];
extern u8 kan_win_fune_tex[];
extern u8 kan_win_yane_tex[];
extern u8 kan_win_ply_tex[];

#define MM_TEX_FULL_ALPHA 235 /* overall opacity, out of 255 */
#define MM_REFRESH_FRAMES 30.0f

static const mMP_Ovl_c* s_map = NULL;
static f32 s_alpha = 0.0f;         /* 0..1, fades on toggle */
static f32 s_refresh_timer = 0.0f;
static Mtx s_ortho;
static int s_ortho_ready = FALSE;

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

static int mm_q(f32 v) {
    return (int)(v * 4.0f + 0.5f);
}

/* Texture rectangle in 320x240 stretch space; texels are the full
 * tex_w x tex_h image. */
static Gfx* mm_texrect(Gfx* gfx, f32 x0, f32 y0, f32 x1, f32 y1, int tex_w, int tex_h) {
    int dsdx = (int)(((f32)tex_w / (x1 - x0)) * 1024.0f);
    int dtdy = (int)(((f32)tex_h / (y1 - y0)) * 1024.0f);

    return gfx_gSPScisTextureRectangle1(gfx, mm_q(x0), mm_q(y0), mm_q(x1), mm_q(y1), G_TX_RENDERTILE, 0, 0, dsdx,
                                        dtdy);
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
    return mm_texrect(gfx, cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f, 16, 16);
}

/* Colours from the map screen's models (kan_hyouji2.c, kan_eki.c, ...). */
static const MMIcon mm_building_icons[mMP_LABEL_NUM] = {
    /* mMP_LABEL_NPC, mMP_LABEL_PLAYER: drawn per house below */
    { NULL, 0, { 0, 0, 0 }, { 0, 0, 0 } },
    { NULL, 0, { 0, 0, 0 }, { 0, 0, 0 } },
    { kan_win_omise_tex, FALSE, { 195, 80, 45 }, { 0, 0, 0 } },        /* shop */
    { kan_win_kouban_tex, FALSE, { 195, 80, 45 }, { 0, 0, 0 } },       /* police */
    { kan_win_yubin_tex, FALSE, { 190, 70, 60 }, { 0, 0, 0 } },        /* post office */
    { kan_win_yashiro_tex, FALSE, { 190, 70, 60 }, { 0, 0, 0 } },      /* wishing well */
    { kan_win_eki_tex, TRUE, { 255, 255, 255 }, { 100, 100, 100 } },   /* station */
    { kan_win_gomi_tex, FALSE, { 190, 70, 60 }, { 0, 0, 0 } },         /* dump */
    { kan_win_mu_tex, FALSE, { 190, 70, 60 }, { 0, 0, 0 } },           /* museum */
    { kan_win_ta_tex, FALSE, { 190, 70, 60 }, { 0, 0, 0 } },           /* tailor */
    { kan_win_fune_tex, TRUE, { 255, 255, 255 }, { 100, 100, 100 } },  /* dock */
};

/* Villager roof colours by cliff layer (kan_win_npc2T_*_model). */
static const u8 mm_roof_prim[3][3] = { { 90, 90, 225 }, { 145, 70, 205 }, { 170, 115, 20 } };

static int mm_label_matches(int label, mActor_name_t item) {
    switch (label) {
        case mMP_LABEL_SHOP:    return item >= SHOP0 && item <= SHOP3;
        case mMP_LABEL_POLICE:  return item == POLICE_STATION;
        case mMP_LABEL_POST:    return item == POST_OFFICE;
        case mMP_LABEL_SHRINE:  return item == WISHING_WELL;
        case mMP_LABEL_STATION: return item == TRAIN_STATION;
        case mMP_LABEL_JUNK:    return item == DUMP;
        case mMP_LABEL_MUSEUM:  return item == MUSEUM;
        case mMP_LABEL_NEEDLE:  return item == NEEDLEWORK_SHOP;
        case mMP_LABEL_PORT:    return item == BOAT || item == PORT_SIGN;
    }
    return FALSE;
}

/* Position of an acre's building inside the acre, in acre units (0..1).
 * Falls back to the acre centre when the structure isn't in the FG layer. */
static void mm_building_pos(int label, int fx, int fz, f32* u, f32* v) {
    mFM_fg_c* fg = Save_GetPointer(fg[fz][fx]);
    int ut_x;
    int ut_z;

    for (ut_z = 0; ut_z < UT_Z_NUM; ut_z++) {
        for (ut_x = 0; ut_x < UT_X_NUM; ut_x++) {
            if (mm_label_matches(label, fg->items[ut_z][ut_x])) {
                *u = ((f32)ut_x + 0.5f) / (f32)UT_X_NUM;
                *v = ((f32)ut_z + 0.5f) / (f32)UT_Z_NUM;
                return;
            }
        }
    }

    *u = 0.5f;
    *v = 0.5f;
}

static void mm_set_vtx(Vtx* vtx, f32 x, f32 y) {
    vtx->v.ob[0] = (s16)((x - 160.0f) * mFont_SCALE_F);
    vtx->v.ob[1] = (s16)((120.0f - y) * mFont_SCALE_F);
    vtx->v.ob[2] = 0;
    vtx->v.flag = 1;
    vtx->v.tc[0] = 0;
    vtx->v.tc[1] = 0;
    vtx->v.cn[0] = 255;
    vtx->v.cn[1] = 255;
    vtx->v.cn[2] = 255;
    vtx->v.cn[3] = 255;
}

/* Arrow pointing where the player faces: angle 0 faces south (down on
 * the map), 0x4000 east. xs squeezes x to undo the widescreen stretch. */
static Gfx* mm_arrow(Gfx* gfx, Vtx* vtx, f32 cx, f32 cy, s16 angle, f32 size, f32 xs, int r, int g, int b, int a) {
    f32 dx = sin_s(angle);
    f32 dy = cos_s(angle);
    f32 px = -dy;
    f32 py = dx;

    mm_set_vtx(&vtx[0], cx + dx * size * xs, cy + dy * size);
    mm_set_vtx(&vtx[1], cx + (-dx * 0.6f + px * 0.7f) * size * xs, cy + (-dy * 0.6f + py * 0.7f) * size);
    mm_set_vtx(&vtx[2], cx + (-dx * 0.6f - px * 0.7f) * size * xs, cy + (-dy * 0.6f - py * 0.7f) * size);

    gDPPipeSync(gfx++);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    gSPVertex(gfx++, vtx, 3, 0);
    gSPNTrianglesInit_5b(gfx++, 1, 0, 1, 2, 0, 0, 0, 0, 0, 0);
    return gfx;
}

void pc_minimap_draw(GAME_PLAY* play) {
    static const f32 tile_sizes[3] = { 12.0f, 16.0f, 21.0f };
    static const s16 home_x[PLAYER_NUM] = { 2128, 2352, 2128, 2352 };
    static const s16 home_z[PLAYER_NUM] = { 1488, 1488, 1768, 1768 };
    static const MMIcon home_icon = { kan_win_ply_tex, TRUE, { 110, 120, 165 }, { 235, 235, 80 } };
    static const MMIcon my_home_icon = { kan_win_ply_tex, TRUE, { 230, 90, 50 }, { 255, 235, 120 } };
    GRAPH* graph;
    PLAYER_ACTOR* player;
    Gfx* poly;
    Gfx* gfx;
    Vtx* vtx;
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

    if (!s_ortho_ready) {
        mFont_CulcOrthoMatrix(&s_ortho);
        s_ortho_ready = TRUE;
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
    icon = tile_h * 0.62f;
    alpha = (int)(s_alpha * MM_TEX_FULL_ALPHA);
    player = GET_PLAYER_ACTOR(play);

    vtx = GRAPH_ALLOC_TYPE(graph, Vtx, 6);

    OPEN_DISP(graph);
    poly = NOW_POLY_OPA_DISP;
    gfx = gfxopen(poly);
    gSPDisplayList(NOW_FONT_DISP++, gfx);

    gDPNoOpTag(gfx++, PC_NOOP_WIDESCREEN_STRETCH);
    gSPMatrix(gfx++, &s_ortho, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gfx++, &Mtx_clear, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPLoadGeometryMode(gfx++, 0);
    gSPTexture(gfx++, 0, 0, 0, G_TX_RENDERTILE, G_ON);

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
                         32, 32);
    }

    /* Building icons, at the building when it can be found in the acre. */
    gfx = mm_mode(gfx, G_TT_NONE);
    for (i = 0; i < FG_BLOCK_TOTAL_NUM; i++) {
        int fx = i % FG_BLOCK_X_NUM;
        int fz = i / FG_BLOCK_X_NUM;
        int label = s_map->label_info[fz][fx].label_no;

        if (label < mMP_LABEL_SHOP || label >= mMP_LABEL_NUM) {
            continue;
        }

        mm_building_pos(label, fx, fz, &u, &v);
        gfx = mm_icon(gfx, &mm_building_icons[label], x0 + (fx + u) * tile_w, y0 + (fz + v) * tile_h, icon * xs,
                      icon, alpha);
    }

    /* Villager houses (the house sits one unit above home_info.ut_z). */
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
        roof.prim[0] = mm_roof_prim[layer][0];
        roof.prim[1] = mm_roof_prim[layer][1];
        roof.prim[2] = mm_roof_prim[layer][2];
        roof.env[0] = roof.env[1] = roof.env[2] = 225;

        u = (f32)(home->block_x - 1) + ((f32)home->ut_x + 0.5f) / (f32)UT_X_NUM;
        v = (f32)(home->block_z - 1) + ((f32)home->ut_z - 0.5f) / (f32)UT_Z_NUM;
        gfx = mm_icon(gfx, &roof, x0 + u * tile_w, y0 + v * tile_h, icon * 0.85f * xs, icon * 0.85f, alpha);
    }

    /* Player houses that have an owner; the current player's own house
     * stands out. */
    for (i = 0; i < PLAYER_NUM; i++) {
        int owner = mHS_get_pl_no(i);
        int mine;

        if (owner >= PLAYER_NUM || mPr_CheckPrivate(Save_GetPointer(private_data[owner])) != TRUE) {
            continue;
        }

        mine = (owner == Common_Get(player_no));
        u = (f32)home_x[i] / mFI_BK_WORLDSIZE_X_F - 1.0f;
        v = ((f32)home_z[i] - mFI_UT_WORLDSIZE_Z_F) / mFI_BK_WORLDSIZE_Z_F - 1.0f;
        gfx = mm_icon(gfx, mine ? &my_home_icon : &home_icon, x0 + u * tile_w, y0 + v * tile_h, icon * 0.85f * xs,
                      icon * 0.85f, alpha);
    }

    /* Player arrow: white outline, then the map screen's red marker. */
    u = player->actor_class.world.position.x / mFI_BK_WORLDSIZE_X_F - 1.0f;
    v = player->actor_class.world.position.z / mFI_BK_WORLDSIZE_Z_F - 1.0f;
    if (u < 0.0f) u = 0.0f;
    if (u > (f32)FG_BLOCK_X_NUM) u = (f32)FG_BLOCK_X_NUM;
    if (v < 0.0f) v = 0.0f;
    if (v > (f32)FG_BLOCK_Z_NUM) v = (f32)FG_BLOCK_Z_NUM;

    gfx = mm_mode(gfx, G_TT_NONE);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gfx = mm_arrow(gfx, &vtx[0], x0 + u * tile_w, y0 + v * tile_h, player->actor_class.shape_info.rotation.y,
                   tile_h * 0.5f, xs, 255, 255, 255, (int)(s_alpha * 255.0f));
    gfx = mm_arrow(gfx, &vtx[3], x0 + u * tile_w, y0 + v * tile_h, player->actor_class.shape_info.rotation.y,
                   tile_h * 0.34f, xs, 255, 70, 30, (int)(s_alpha * 255.0f));

    gDPPipeSync(gfx++);
    gDPNoOpTag(gfx++, PC_NOOP_WIDESCREEN_STRETCH_OFF);
    gSPEndDisplayList(gfx++);

    gfxclose(poly, gfx);
    SET_POLY_OPA_DISP(gfx);
    CLOSE_DISP(graph);
}
