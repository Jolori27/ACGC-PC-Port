/* pc_car.c - F5 debug car: Gracie's car from the designer event, drivable.
 *
 * The car is not an actor: it is a position/heading drawn from the player
 * draw, and the player itself does the driving in mPlayer_INDEX_PC_DRIVE
 * (src/game/m_player_main_pc_drive.c_inc), so the vanilla collision and
 * acre streaming handle it like walking.
 *
 * The model only has the side the fixed event camera sees (wheels on one
 * side), so a mirrored copy with front-face culling draws the other half. */
#include "pc_car.h"
#include "pc_platform.h"
#include "pc_settings_menu.h"
#include "pc_typing.h"
#include "pc_pause_menu.h"

#include "libforest/gbi_extensions.h"
#include "PR/gbi.h"
#include "m_play.h"
#include "m_common_data.h"
#include "m_rcp.h"
#include "sys_matrix.h"
#include "bg_item_h.h"
#include "graph.h"

PcCar g_pc_car;

static int s_toggle_request;

void pc_car_request_toggle(void) {
    s_toggle_request = TRUE;
}

int pc_car_take_toggle_request(void) {
    int request = s_toggle_request;

    s_toggle_request = FALSE;
    return request;
}

int pc_car_toggle_allowed(void) {
    return !pc_settings_menu_active() && !g_pc_typing_mode && !g_pc_editor_active && !g_pc_nes_active &&
           !g_pc_title_main_menu_visible && !g_pc_model_viewer;
}

void pc_car_world_to_local(const xyz_t* pos, f32* lx, f32* lz) {
    f32 s = sin_s(g_pc_car.angle);
    f32 c = cos_s(g_pc_car.angle);
    f32 dx = pos->x - g_pc_car.pos.x;
    f32 dz = pos->z - g_pc_car.pos.z;

    *lx = dx * c - dz * s;
    *lz = dx * s + dz * c;
}

void pc_car_local_to_world(f32 lx, f32 lz, xyz_t* pos) {
    f32 s = sin_s(g_pc_car.angle);
    f32 c = cos_s(g_pc_car.angle);

    pos->x = g_pc_car.pos.x + lx * c + lz * s;
    pos->z = g_pc_car.pos.z - lx * s + lz * c;
}

f32 pc_car_box_x_min(void) {
    return (PC_CAR_BOX_X_MIN - PC_CAR_SEAT_X) * PC_CAR_SCALE;
}

f32 pc_car_box_x_max(void) {
    return (PC_CAR_BOX_X_MAX - PC_CAR_SEAT_X) * PC_CAR_SCALE;
}

f32 pc_car_box_z_min(void) {
    return (PC_CAR_BOX_Z_MIN - PC_CAR_SEAT_Z) * PC_CAR_SCALE;
}

f32 pc_car_box_z_max(void) {
    return (PC_CAR_BOX_Z_MAX - PC_CAR_SEAT_Z) * PC_CAR_SCALE;
}

/* ---- Model (src/data/model/obj_s_car.c) ---- */

extern Gfx s_car_DL_model[];
extern u16 obj_s_car_pal[];
extern u8 obj_s_car_t1_tex[];
extern u8 obj_s_car_t2_tex[];
extern u8 obj_s_car_t3_tex[];
extern Vtx obj_s_car_v[];
extern Vtx obj_car_shadow_v[];
extern Gfx obj_car_shadowT_model[];

/* Same lists as obj_s_car_*T_model with the culled face flipped: drawn
 * through a mirroring matrix, the kept faces wind the other way. */
#define PC_CAR_MIRROR_GEOMETRY \
    (G_ZBUFFER | G_SHADE | G_CULL_FRONT | G_FOG | G_LIGHTING | G_SHADING_SMOOTH | G_DECAL_LEQUAL)

static Gfx pc_car_mirror_t1[] = {
    gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_ON),
    gsDPSetCombineLERP(TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, TEXEL0, COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2),
    gsDPLoadTLUT_Dolphin(15, 16, 1, obj_s_car_pal),
    gsDPSetTextureImage_Dolphin(G_IM_FMT_CI, G_IM_SIZ_4b, 128, 32, obj_s_car_t1_tex),
    gsDPSetTile_Dolphin(G_DOLPHIN_TLUT_DEFAULT_MODE, 0, 15, GX_CLAMP, GX_MIRROR, 0, 0),
    gsSPLoadGeometryMode(PC_CAR_MIRROR_GEOMETRY),
    gsSPVertex(obj_s_car_v, 32, 0),
    gsSPNTrianglesInit_5b(20, 0, 1, 2, 3, 2, 4, 1, 5, 2),
    gsSPNTriangles_5b(6, 7, 8, 9, 10, 11, 5, 4, 2, 6, 12, 7),
    gsSPNTriangles_5b(13, 14, 11, 10, 13, 11, 3, 0, 2, 7, 15, 8),
    gsSPNTriangles_5b(16, 15, 7, 12, 16, 7, 17, 18, 19, 20, 21, 22),
    gsSPNTriangles_5b(22, 23, 20, 21, 24, 22, 25, 19, 18, 26, 27, 28),
    gsSPNTriangles_5b(29, 30, 31, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    gsSPVertex(&obj_s_car_v[32], 14, 0),
    gsSPNTrianglesInit_5b(6, 0, 1, 2, 0, 2, 3, 4, 5, 6),
    gsSPNTriangles_5b(7, 8, 9, 10, 11, 12, 10, 12, 13, 0, 0, 0),
    gsSPEndDisplayList(),
};

static Gfx pc_car_mirror_t2[] = {
    gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_ON),
    gsDPSetCombineLERP(TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, TEXEL0, COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2),
    gsDPLoadTLUT_Dolphin(15, 16, 1, obj_s_car_pal),
    gsDPSetTextureImage_Dolphin(G_IM_FMT_CI, G_IM_SIZ_4b, 128, 32, obj_s_car_t2_tex),
    gsDPSetTile_Dolphin(G_DOLPHIN_TLUT_DEFAULT_MODE, 0, 15, GX_CLAMP, GX_MIRROR, 0, 0),
    gsSPLoadGeometryMode(PC_CAR_MIRROR_GEOMETRY),
    gsSPVertex(&obj_s_car_v[46], 13, 0),
    gsSPNTrianglesInit_5b(9, 0, 1, 2, 1, 3, 2, 1, 4, 3),
    gsSPNTriangles_5b(2, 3, 5, 3, 6, 5, 6, 3, 7, 6, 8, 9),
    gsSPNTriangles_5b(8, 6, 7, 10, 11, 12, 0, 0, 0, 0, 0, 0),
    gsSPEndDisplayList(),
};

static Gfx pc_car_mirror_t3[] = {
    gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_ON),
    gsDPSetCombineLERP(TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, TEXEL0, COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2),
    gsDPLoadTLUT_Dolphin(15, 16, 1, obj_s_car_pal),
    gsDPSetTextureImage_Dolphin(G_IM_FMT_CI, G_IM_SIZ_4b, 128, 32, obj_s_car_t3_tex),
    gsDPSetTile_Dolphin(G_DOLPHIN_TLUT_DEFAULT_MODE, 0, 15, GX_MIRROR, GX_MIRROR, 0, 0),
    gsSPLoadGeometryMode(PC_CAR_MIRROR_GEOMETRY),
    gsSPVertex(&obj_s_car_v[59], 21, 0),
    gsSPNTrianglesInit_5b(13, 0, 1, 2, 3, 4, 5, 6, 3, 5),
    gsSPNTriangles_5b(5, 4, 7, 6, 5, 7, 8, 9, 10, 9, 11, 10),
    gsSPNTriangles_5b(8, 12, 9, 12, 11, 9, 13, 14, 15, 13, 15, 16),
    gsSPNTriangles_5b(17, 18, 19, 17, 19, 20, 0, 0, 0, 0, 0, 0),
    gsSPEndDisplayList(),
};

static Gfx pc_car_mirror_inner[] = {
    gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_ON),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0, 0, 0, 0, TEXEL0, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2),
    gsDPLoadTLUT_Dolphin(15, 16, 1, obj_s_car_pal),
    gsDPSetTextureImage_Dolphin(G_IM_FMT_CI, G_IM_SIZ_4b, 128, 32, obj_s_car_t3_tex),
    gsDPSetTile_Dolphin(G_DOLPHIN_TLUT_DEFAULT_MODE, 0, 15, GX_MIRROR, GX_MIRROR, 0, 0),
    gsDPSetPrimColor(0, 128, 255, 255, 255, 255),
    gsSPLoadGeometryMode(PC_CAR_MIRROR_GEOMETRY),
    gsSPVertex(&obj_s_car_v[80], 30, 0),
    /* The last two triangles of the original list (the steering wheel,
     * vertices 106..109) are left out: one wheel, on the driver's side. */
    gsSPNTrianglesInit_5b(16, 0, 1, 2, 0, 2, 3, 4, 5, 6),
    gsSPNTriangles_5b(4, 6, 7, 8, 9, 10, 8, 10, 11, 12, 13, 14),
    gsSPNTriangles_5b(12, 15, 13, 15, 16, 13, 17, 18, 15, 17, 15, 12),
    gsSPNTriangles_5b(19, 20, 21, 22, 19, 21, 22, 21, 23, 24, 25, 18),
    gsSPNTriangles_5b(24, 18, 17, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    gsSPEndDisplayList(),
};

static Gfx pc_car_mirror_DL[] = {
    gsSPDisplayList(pc_car_mirror_t1),
    gsSPDisplayList(pc_car_mirror_t2),
    gsSPDisplayList(pc_car_mirror_t3),
    gsSPDisplayList(pc_car_mirror_inner),
    gsSPEndDisplayList(),
};

/* Copy of the designer car's shadow setup (ac_s_car.c). */
static u8 pc_car_shadow_fix_flags[24] = {
    FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
    TRUE,  TRUE,  TRUE,  TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
};

static bIT_ShadowData_c pc_car_shadow_data = {
    23, pc_car_shadow_fix_flags, 60.0f, obj_car_shadow_v, obj_car_shadowT_model,
};

/* Seat anchor -> car-aligned model space, then undo the 45 degree authoring
 * rotation (model +X+Z diagonal becomes the car's side, -X+Z its hood).
 * lift_y raises the body so the wheels touch the ground; the shadow stays
 * flat on it. */
static void pc_car_load_matrix(int mirrored, f32 lift_y) {
    s_xyz rot = { 0, 0, 0 };

    rot.y = g_pc_car.angle;
    Matrix_softcv3_load(&rot, g_pc_car.pos.x, g_pc_car.pos.y, g_pc_car.pos.z);
    Matrix_scale(PC_CAR_SCALE, PC_CAR_SCALE, PC_CAR_SCALE, MTX_MULT);
    Matrix_translate(-PC_CAR_SEAT_X, lift_y, -PC_CAR_SEAT_Z, MTX_MULT);
    if (mirrored) {
        Matrix_scale(-1.0f, 1.0f, 1.0f, MTX_MULT);
    }
    Matrix_RotateY(DEG2SHORT_ANGLE2(45.0f), MTX_MULT);
}

void pc_car_draw(GAME* game) {
    GRAPH* graph = game->graph;
    int mirrored;

    if (!g_pc_car.active) {
        return;
    }

    Matrix_push();
    _texture_z_light_fog_prim_npc(graph);

    /* The designer car is a Dolphin-authored model (S_Car_Profile has
     * ACTOR_STATE_TA_SET); the player draw runs in N64 texture mode, so set
     * the same state Actor_draw_ta_set would and restore it afterwards. */
    OPEN_DISP(graph);
    gDPSetTextureAdjustMode(NEXT_POLY_OPA_DISP, G_TA_DOLPHIN);
    gDPSetTextureAdjustMode(NEXT_SHADOW_DISP, G_TA_DOLPHIN);
    gDPSetTexEdgeAlpha(NEXT_POLY_OPA_DISP, 127);
    gDPSetTexEdgeAlpha(NEXT_SHADOW_DISP, 127);
    CLOSE_DISP(graph);

    for (mirrored = 0; mirrored < 2; mirrored++) {
        pc_car_load_matrix(mirrored, PC_CAR_WHEEL_BOTTOM);

        OPEN_DISP(graph);
        gDPPipeSync(NEXT_POLY_OPA_DISP);
        gDPSetPrimColor(NEXT_POLY_OPA_DISP, 0, 128, 255, 255, 255, 255);
        gSPMatrix(NEXT_POLY_OPA_DISP, _Matrix_to_Mtx_new(graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(NEXT_POLY_OPA_DISP, mirrored ? pc_car_mirror_DL : s_car_DL_model);
        CLOSE_DISP(graph);
    }

    /* The shadow mesh covers the whole footprint: draw it once. */
    if (Common_Get(clip).bg_item_clip != NULL && Common_Get(clip).bg_item_clip->draw_shadow_proc != NULL) {
        pc_car_load_matrix(FALSE, 0.0f);
        (*Common_Get(clip).bg_item_clip->draw_shadow_proc)(game, &pc_car_shadow_data, FALSE);
    }

    OPEN_DISP(graph);
    gDPSetTextureAdjustMode(NEXT_POLY_OPA_DISP, G_TA_N64);
    gDPSetTextureAdjustMode(NEXT_SHADOW_DISP, G_TA_N64);
    gDPSetTexEdgeAlpha(NEXT_POLY_OPA_DISP, 144);
    gDPSetTexEdgeAlpha(NEXT_SHADOW_DISP, 144);
    CLOSE_DISP(graph);

    Matrix_pull();
}
