/* pc_save_repair.c - repair towns damaged by the first PC quick save
 *
 * While the town is running, every building, mailbox, Gyroid and event
 * structure overwrites its own tile in the town map (which is the save data
 * itself) with RSV_NO, then with a DUMMY_xxx placeholder once it has been on
 * screen. The game writes the real item back only when the town is torn
 * down, before every save. The first version of the PC quick save wrote the
 * map while the town was running, so the buildings around the player were
 * saved as placeholders and never spawned again.
 *
 * This runs when a saved town is continued, before the town is set up, and
 * puts the real items back:
 *  - every DUMMY_xxx placeholder becomes the item that wrote it (exactly
 *    what the game's restore_fgdata does);
 *  - RSV_NO left where a building that never reached the screen stood is
 *    rebuilt from the town's ROM acre templates (the same data the game used
 *    to make the town), from the 12 fixed tiles of the player-house acre,
 *    and from each villager's saved home for villager houses;
 *  - the tiles those actors reserve in front of their doors are freed.
 * A healthy save never holds a placeholder, and the reserved-tile rules only
 * run when placeholders or a reserved house tile prove the damage, so a
 * healthy save is never changed. The damaged file is copied aside first. */
#include "pc_platform.h"
#include "pc_quicksave.h"

#include "m_common_data.h"
#include "m_field_make.h"
#include "m_random_field.h"
#include "m_name_table.h"
#include "m_npc.h"
#include "m_shop.h"

#include <stdio.h>
#include <string.h>

#define FG_UNITS_X (FG_BLOCK_X_NUM * UT_X_NUM)
#define FG_UNITS_Z (FG_BLOCK_Z_NUM * UT_Z_NUM)

/* Placeholders with no define in m_name_table.h */
#define PC_DUMMY_TOUDAI 0xF11E /* ac_toudai_move.c_inc */
#define PC_DUMMY_KAGO 0xF111   /* ac_kago_move.c_inc, same value as DUMMY_TURI */

static mFM_fg_c s_tmpl[FG_BLOCK_Z_NUM][FG_BLOCK_X_NUM];
static int s_have_tmpl;
static int s_repaired;

/* Global unit coordinates over the whole town map. */
static mActor_name_t* fg_at(int gx, int gz) {
    if (gx < 0 || gz < 0 || gx >= FG_UNITS_X || gz >= FG_UNITS_Z) {
        return NULL;
    }
    return &Save_Get(fg[gz / UT_Z_NUM][gx / UT_X_NUM]).items[gz % UT_Z_NUM][gx % UT_X_NUM];
}

static mActor_name_t tmpl_at(int gx, int gz) {
    if (!s_have_tmpl || gx < 0 || gz < 0 || gx >= FG_UNITS_X || gz >= FG_UNITS_Z) {
        return EMPTY_NO;
    }
    return s_tmpl[gz / UT_Z_NUM][gx / UT_X_NUM].items[gz % UT_Z_NUM][gx % UT_X_NUM];
}

static u32 block_kind(int gx, int gz) {
    int combi = Save_Get(combi_table[gz / UT_Z_NUM + 1][gx / UT_X_NUM + 1]).combination_type;

    if (combi < 0 || combi >= data_combi_table_number) {
        return 0;
    }
    return mRF_Type2BlockInfo(data_combi_table[combi].type);
}

static void set_tile(int gx, int gz, mActor_name_t item, const char* why) {
    mActor_name_t* p = fg_at(gx, gz);

    if (p == NULL || *p == item) {
        return;
    }
    printf("[REPAIR] acre %d-%d unit (%d,%d): %04X -> %04X (%s)\n", gz / UT_Z_NUM + 1, gx / UT_X_NUM + 1,
           gx % UT_X_NUM, gz % UT_Z_NUM, *p, item, why);
    *p = item;
    s_repaired++;
}

/* A door tile the actor reserved (RSV_NO) and frees again on teardown. */
static void free_reserved(int gx, int gz) {
    mActor_name_t* p = fg_at(gx, gz);

    if (p != NULL && *p == RSV_NO) {
        set_tile(gx, gz, EMPTY_NO, "door tile");
    }
}

static int is_placeholder(mActor_name_t v) {
    return v >= DUMMY_MAILBOX0 && v <= 0xF128;
}

/* The real item for a placeholder at (gx, gz), or RSV_NO if it can't be
 * resolved (then it is left alone). */
static mActor_name_t placeholder_real(mActor_name_t v, int gx, int gz) {
    int ux = gx % UT_X_NUM;
    int uz = gz % UT_Z_NUM;
    mActor_name_t t = tmpl_at(gx, gz);

    if (v >= DUMMY_MAILBOX0 && v <= DUMMY_MAILBOX3) {
        return ACTOR_PROP_MAILBOX0 + (v - DUMMY_MAILBOX0);
    }
    if (v >= DUMMY_NPC_HOUSE_START && v <= DUMMY_NPC_HOUSE_END) {
        return NPC_HOUSE_START + (v - DUMMY_NPC_HOUSE_START);
    }
    if (v >= DUMMY_HOUSE0 && v <= DUMMY_SHOP3) { /* HOUSE0..3, SHOP0..3 */
        return HOUSE0 + (v - DUMMY_HOUSE0);
    }
    if (v >= DUMMY_HANIWA0 && v <= DUMMY_HANIWA3) {
        return ACTOR_PROP_HANIWA0 + (v - DUMMY_HANIWA0);
    }

    switch (v) {
        case DUMMY_POST_OFFICE: /* == DUMMY_NEEDLEWORK_SHOP */
            if (block_kind(gx, gz) & mRF_BLOCKKIND_POSTOFFICE) {
                return POST_OFFICE;
            }
            if (block_kind(gx, gz) & mRF_BLOCKKIND_TAILORS) {
                return NEEDLEWORK_SHOP;
            }
            return (t == POST_OFFICE || t == NEEDLEWORK_SHOP) ? t : RSV_NO;
        case DUMMY_STATION:
            return TRAIN_STATION;
        case DUMMY_POLICE_STATION:
            return POLICE_STATION;
        case DUMMY_RESERVE:
            if (t == PORT_SIGN) {
                return PORT_SIGN;
            }
            /* The lot's sign number was only in this tile; the game picks a
             * free one the same way when it puts a lot sign back. */
            return mFM_GetReseveName(gx / UT_X_NUM + 1, gz / UT_Z_NUM + 1);
        case DUMMY_SHRINE:
            return WISHING_WELL;
        case DUMMY_BROKER_SHOP:
            return BROKER_TENT;
        case DUMMY_BUGGY:
            return FORTUNE_TENT;
        case DUMMY_CAR:
            return DESIGNER_CAR;
        case DUMMY_KAMAKURA:
            return KAMAKURA;
        case DUMMY_GOZA:
            if (ux == 11 && uz == 7) return SAKURA_TABLE0;
            if (ux == 5 && uz == 11) return SAKURA_TABLE1;
            return RSV_NO;
        case DUMMY_RADIO:
            return AEROBICS_RADIO;
        case DUMMY_YATAI: /* == DUMMY_TUKIMI, whose structures are never placed */
            if (ux == 3 && (uz == 11 || uz == 2)) return FIREWORKS_STALL0;
            if (ux == 12 && (uz == 11 || uz == 10 || uz == 2)) return FIREWORKS_STALL1;
            return RSV_NO;
        case DUMMY_MIKUJI:
            return NEWYEAR_TABLE;
        case DUMMY_COUNT:
            return NEWYEAR_COUNTDOWN0;
        case DUMMY_COUNT02:
            return NEWYEAR_COUNTDOWN1;
        case DUMMY_TAMA:
            if (ux == 3 && uz == 7) return SPORTSFAIR_BALLS_RED;
            if (ux == 12 && uz == 7) return SPORTSFAIR_BALLS_WHITE;
            return RSV_NO;
        case DUMMY_TURI: /* == PC_DUMMY_KAGO */
            if ((ux == 12 && uz == 12) || (ux == 3 && uz == 3)) return FISHCHECK_STAND0;
            if ((ux == 3 && uz == 12) || (ux == 12 && uz == 3)) return FISHCHECK_STAND1;
            if (ux == 3 && uz == 8) return SPORTSFAIR_BASKET_RED;
            if (ux == 12 && uz == 8) return SPORTSFAIR_BASKET_WHITE;
            return RSV_NO;
        case DUMMY_KOINOBORI:
            return KOINOBORI_WINDSOCK;
        case DUMMY_DUMP:
            return DUMP;
        case DUMMY_WINDMILL:
            return (t >= WINDMILL0 && t <= WINDMILL4) ? t : RSV_NO;
        case DUMMY_LOTUS:
            return LOTUS;
        case DUMMY_MIKANBOX:
            return MIKANBOX;
        case DUMMY_DOUZOU:
            return DOUZOU;
        case PC_DUMMY_TOUDAI:
            return TOUDAI;
        case DUMMY_NAMEPLATE:
            return ACTOR_PROP_VILLAGER_SIGNBOARD;
        case DUMMY_MUSEUM:
            return MUSEUM;
        case DUMMY_BRIDGE:
            if (t == RSV_BRIDGE1) return BRIDGE_A1;
            if (t == RSV_BRIDGE0) return BRIDGE_A0;
            return RSV_NO;
        case DUMMY_GHOG:
            return GHOG;
        case DUMMY_HTABLE:
            if (ux == 4 && uz == 5) return HTABLE0;
            if (ux == 11 && uz == 7) return HTABLE1;
            if (ux == 5 && uz == 10) return HTABLE2;
            return RSV_NO;
        case DUMMY_TENT:
            return TENT;
        case DUMMY_BOAT:
            return BOAT;
    }

    return RSV_NO;
}

/* Door tiles reserved by the actor of a restored item. */
static void free_door_tiles(mActor_name_t real, int gx, int gz) {
    if (real == FORTUNE_TENT || real == KAMAKURA || real == TENT || real == BROKER_TENT) {
        free_reserved(gx, gz + 2);
    } else if (real == TOUDAI) {
        free_reserved(gx - 1, gz - 2);
        free_reserved(gx, gz - 2);
    }
}

/* The 12 fixed tiles of the player-house acre (Save fg[1][2]). */
static const struct {
    u8 uz;
    u8 ux;
    mActor_name_t item;
} l_home_tiles[12] = {
    { 3, 3, HOUSE0 },
    { 3, 12, HOUSE0 + 1 },
    { 10, 3, HOUSE0 + 2 },
    { 10, 12, HOUSE0 + 3 },
    { 3, 5, ACTOR_PROP_MAILBOX0 },
    { 3, 10, ACTOR_PROP_MAILBOX0 + 1 },
    { 10, 5, ACTOR_PROP_MAILBOX0 + 2 },
    { 10, 10, ACTOR_PROP_MAILBOX0 + 3 },
    { 5, 3, ACTOR_PROP_HANIWA0 },
    { 5, 12, ACTOR_PROP_HANIWA0 + 1 },
    { 12, 3, ACTOR_PROP_HANIWA0 + 2 },
    { 12, 12, ACTOR_PROP_HANIWA0 + 3 },
};
#define HOME_BX 2
#define HOME_BZ 1

/* TRUE if a villager's house stands (or should stand) on this tile. */
static int qs_is_villager_home(int gx, int gz) {
    int i;

    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        Animal_c* animal = Save_GetPointer(animals[i]);

        if (ITEM_NAME_GET_TYPE(animal->id.npc_id) != NAME_TYPE_NPC || animal->home_info.block_x < 1 ||
            animal->home_info.block_z < 1) {
            continue;
        }
        if ((animal->home_info.block_x - 1) * UT_X_NUM + animal->home_info.ut_x == gx &&
            (animal->home_info.block_z - 1) * UT_Z_NUM + animal->home_info.ut_z - 1 == gz) {
            return TRUE;
        }
    }
    return FALSE;
}

/* A building of the acre templates whose anchor tile is RSV_NO. That never
 * happens in a healthy save (anchors keep their id; lot signs can become a
 * villager house, so they don't count). */
static int template_anchor_lost(int gx, int gz, mActor_name_t* t_out) {
    mActor_name_t t = tmpl_at(gx, gz);
    int type = ITEM_NAME_GET_TYPE(t);

    if (*fg_at(gx, gz) != RSV_NO || (type != NAME_TYPE_STRUCT && type != NAME_TYPE_PROPS)) {
        return FALSE;
    }
    if (t >= NPC_HOUSE_START && t < STRUCTURE_START) {
        return FALSE; /* villager houses: handled from the villagers */
    }
    if (t >= SIGN00 && t <= SIGN20) {
        return FALSE;
    }
    if (t_out != NULL) {
        *t_out = t;
    }
    return TRUE;
}

/* The mayor's bridge, built on the first RSV_BRIDGE0 (else RSV_BRIDGE1) tile
 * of its acre's template, left as RSV_NO because it was never on screen.
 * Its six neighbouring tiles are legitimately RSV_NO and are not touched. */
static int bridge_anchor_lost(int* out_gx, int* out_gz, mActor_name_t* item) {
    PlusBridge_c* bridge = Save_GetPointer(bridge);
    int bx;
    int bz;
    int pass;
    int i;

    if (!s_have_tmpl || !bridge->exists || bridge->block_x < 1 || bridge->block_x > FG_BLOCK_X_NUM ||
        bridge->block_z < 1 || bridge->block_z > FG_BLOCK_Z_NUM) {
        return FALSE;
    }
    bx = bridge->block_x - 1;
    bz = bridge->block_z - 1;

    for (i = 0; i < UT_TOTAL_NUM; i++) {
        mActor_name_t v = Save_Get(fg[bz][bx]).items[i >> 4][i & 15];

        if (v == BRIDGE_A0 || v == BRIDGE_A1 || v == DUMMY_BRIDGE) {
            return FALSE; /* the bridge is there */
        }
    }

    /* the same search order as the bridge event (bridge_make_in) */
    for (pass = 0; pass < 2; pass++) {
        mActor_name_t marker = pass == 0 ? RSV_BRIDGE0 : RSV_BRIDGE1;

        for (i = 0; i < UT_TOTAL_NUM; i++) {
            if (s_tmpl[bz][bx].items[i >> 4][i & 15] == marker) {
                if (Save_Get(fg[bz][bx]).items[i >> 4][i & 15] != RSV_NO) {
                    return FALSE;
                }
                *out_gx = bx * UT_X_NUM + (i & 15);
                *out_gz = bz * UT_Z_NUM + (i >> 4);
                *item = pass == 0 ? BRIDGE_A0 : BRIDGE_A1;
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* A lot-based event structure (fortune teller's tent, designer car, igloo,
 * camper's tent, Redd's tent) that was never on screen: a 3x3 of RSV_NO on
 * a lot. The game removes it with every save (mEv_finish), so a healthy save
 * never has one; a villager house on a lot keeps its id and signboard. */
static int event_lot_left(int gx, int gz) {
    mActor_name_t t = tmpl_at(gx, gz);
    int dx;
    int dz;

    if (*fg_at(gx, gz) != RSV_NO || t < SIGN00 || t > SIGN20) {
        return FALSE;
    }
    if (gx % UT_X_NUM < 1 || gx % UT_X_NUM > UT_X_NUM - 2 || gz % UT_Z_NUM < 1 || gz % UT_Z_NUM > UT_Z_NUM - 2) {
        return FALSE;
    }
    for (dz = -1; dz <= 1; dz++) {
        for (dx = -1; dx <= 1; dx++) {
            if (*fg_at(gx + dx, gz + dz) != RSV_NO) {
                return FALSE;
            }
        }
    }
    return !qs_is_villager_home(gx, gz);
}

static int damage_evidence(void) {
    int gx;
    int gz;
    int i;

    for (gz = 0; gz < FG_UNITS_Z; gz++) {
        for (gx = 0; gx < FG_UNITS_X; gx++) {
            if (is_placeholder(*fg_at(gx, gz)) ||
                (s_have_tmpl && (template_anchor_lost(gx, gz, NULL) || event_lot_left(gx, gz)))) {
                return TRUE;
            }
        }
    }

    for (i = 0; i < 12; i++) {
        if (Save_Get(fg[HOME_BZ][HOME_BX]).items[l_home_tiles[i].uz][l_home_tiles[i].ux] == RSV_NO) {
            return TRUE;
        }
    }

    {
        mActor_name_t item;

        if (bridge_anchor_lost(&gx, &gz, &item)) {
            return TRUE;
        }
    }

    return FALSE;
}

static void backup_damaged_file(void) {
    static const char* path = "save/card_a/DobutsunomoriP_MURA.gci";
    static const char* bak = "save/card_a/DobutsunomoriP_MURA.gci.before_repair";
    FILE* in;
    FILE* out;
    char buf[8192];
    size_t n;

    in = fopen(bak, "rb");
    if (in != NULL) { /* keep the first one */
        fclose(in);
        return;
    }
    in = fopen(path, "rb");
    if (in == NULL) {
        return;
    }
    out = fopen(bak, "wb");
    if (out != NULL) {
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            fwrite(buf, 1, n, out);
        }
        fclose(out);
        printf("[REPAIR] damaged save copied to %s\n", bak);
    }
    fclose(in);
}

int pc_save_repair_town(void) {
    int gx;
    int gz;
    int i;
    static char toast[64];

    s_repaired = 0;
    s_have_tmpl = mFM_PcGetTownFgTemplate(s_tmpl);
    if (!damage_evidence()) {
        return 0;
    }

    printf("[REPAIR] this town was saved with buildings missing; repairing\n");
    backup_damaged_file();
    if (!s_have_tmpl) {
        printf("[REPAIR] acre templates unavailable, repairing from placeholders only\n");
    }
    mFM_SetBlockKindLoadCombi(); /* mFM_GetReseveName needs the block kinds */

    /* 1. Villager houses first: their tiles decide whether the door tile
     *    below them is freed. A damaged house tile holds RSV_NO or this
     *    villager's placeholder. */
    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        Animal_c* animal = Save_GetPointer(animals[i]);
        mActor_name_t npc = animal->id.npc_id;
        mActor_name_t house;
        mActor_name_t* p;
        int hx;
        int hz;

        if (ITEM_NAME_GET_TYPE(npc) != NAME_TYPE_NPC) {
            continue;
        }
        if (animal->home_info.block_x < 1 || animal->home_info.block_x > FG_BLOCK_X_NUM ||
            animal->home_info.block_z < 1 || animal->home_info.block_z > FG_BLOCK_Z_NUM ||
            animal->home_info.ut_x < 1 || animal->home_info.ut_x > UT_X_NUM - 2 || animal->home_info.ut_z < 2 ||
            animal->home_info.ut_z > UT_Z_NUM - 1) {
            continue;
        }

        hx = (animal->home_info.block_x - 1) * UT_X_NUM + animal->home_info.ut_x;
        hz = (animal->home_info.block_z - 1) * UT_Z_NUM + animal->home_info.ut_z - 1;
        house = NPC_HOUSE_START + (npc - NPC_START);
        p = fg_at(hx, hz);
        if (p == NULL) {
            continue;
        }
        if (*p == RSV_NO || *p == DUMMY_NPC_HOUSE_START + (npc - NPC_START)) {
            set_tile(hx, hz, house, "villager house");
            free_reserved(hx, hz + 2);
            p = fg_at(hx - 1, hz + 1);
            if (p != NULL && (*p == RSV_NO || *p == DUMMY_NAMEPLATE)) {
                set_tile(hx - 1, hz + 1, ACTOR_PROP_VILLAGER_SIGNBOARD, "villager sign");
            }
        }
    }

    /* 2. Every placeholder back to its real item. */
    for (gz = 0; gz < FG_UNITS_Z; gz++) {
        for (gx = 0; gx < FG_UNITS_X; gx++) {
            mActor_name_t v = *fg_at(gx, gz);
            mActor_name_t real;

            if (!is_placeholder(v)) {
                continue;
            }
            real = placeholder_real(v, gx, gz);
            if (real == RSV_NO) {
                printf("[REPAIR] acre %d-%d unit (%d,%d): placeholder %04X left as is\n", gz / UT_Z_NUM + 1,
                       gx / UT_X_NUM + 1, gx % UT_X_NUM, gz % UT_Z_NUM, v);
                continue;
            }
            if (real >= SHOP0 && real <= SHOP3) {
                real = SHOP0 + Save_Get(shop).shop_info.shop_level;
            }
            set_tile(gx, gz, real, "placeholder");
            free_door_tiles(real, gx, gz);
        }
    }

    /* 3. The player-house acre's fixed tiles. */
    for (i = 0; i < 12; i++) {
        int hx = HOME_BX * UT_X_NUM + l_home_tiles[i].ux;
        int hz = HOME_BZ * UT_Z_NUM + l_home_tiles[i].uz;

        if (*fg_at(hx, hz) == RSV_NO) {
            set_tile(hx, hz, l_home_tiles[i].item, "player-house acre");
        }
    }

    /* 4. Buildings of the acre templates that never reached the screen:
     *    their anchor tile was left RSV_NO. Lot signs are skipped (a lot can
     *    hold a villager house now). */
    if (s_have_tmpl) {
        for (gz = 0; gz < FG_UNITS_Z; gz++) {
            for (gx = 0; gx < FG_UNITS_X; gx++) {
                mActor_name_t t;

                if (!template_anchor_lost(gx, gz, &t)) {
                    continue;
                }
                if (t >= SHOP0 && t <= SHOP3) {
                    t = SHOP0 + Save_Get(shop).shop_info.shop_level;
                }
                set_tile(gx, gz, t, "acre template");
                free_door_tiles(t, gx, gz);
            }
        }

        {
            mActor_name_t bridge;

            if (bridge_anchor_lost(&gx, &gz, &bridge)) {
                set_tile(gx, gz, bridge, "bridge");
            }
        }

        /* The police officer reserves the tile two units right of the
         * police box and frees it only in its save proc. */
        for (gz = 0; gz < FG_UNITS_Z; gz++) {
            for (gx = 0; gx < FG_UNITS_X; gx++) {
                mActor_name_t* p = fg_at(gx + 2, gz);

                if (*fg_at(gx, gz) == POLICE_STATION && p != NULL && *p == RSV_NO && tmpl_at(gx + 2, gz) != RSV_NO) {
                    set_tile(gx + 2, gz, EMPTY_NO, "police officer's spot");
                }
            }
        }

        /* Lot event structures never on screen: remove them the way the game
         * does (mFI_SetFGStructure_common REMOVE): free the 3x3 and the door
         * tile, then put a lot sign back. */
        for (gz = 0; gz < FG_UNITS_Z; gz++) {
            for (gx = 0; gx < FG_UNITS_X; gx++) {
                int dx;
                int dz;

                if (!event_lot_left(gx, gz)) {
                    continue;
                }
                for (dz = -1; dz <= 1; dz++) {
                    for (dx = -1; dx <= 1; dx++) {
                        if (dx != 0 || dz != 0) {
                            set_tile(gx + dx, gz + dz, EMPTY_NO, "event structure fill");
                        }
                    }
                }
                free_reserved(gx, gz + 2);
                set_tile(gx, gz, mFM_GetReseveName(gx / UT_X_NUM + 1, gz / UT_Z_NUM + 1), "lot sign (event)");
            }
        }

        /* 5. Lot signs: a lot whose sign was never on screen is a lone
         *    RSV_NO tile (healthy lots and houses have no lone reserved
         *    tile). The sign's number was only in that tile; the game picks
         *    a free one the same way when it puts a lot sign back. */
        for (gz = 0; gz < FG_UNITS_Z; gz++) {
            for (gx = 0; gx < FG_UNITS_X; gx++) {
                mActor_name_t t = tmpl_at(gx, gz);
                int lone = TRUE;
                int dx;
                int dz;

                if (*fg_at(gx, gz) != RSV_NO || t < SIGN00 || t > SIGN20) {
                    continue;
                }
                for (dz = -1; dz <= 1 && lone; dz++) {
                    for (dx = -1; dx <= 1; dx++) {
                        mActor_name_t* n = fg_at(gx + dx, gz + dz);

                        if ((dx != 0 || dz != 0) && n != NULL && *n == RSV_NO) {
                            lone = FALSE;
                            break;
                        }
                    }
                }
                if (!lone || qs_is_villager_home(gx, gz)) {
                    continue;
                }
                set_tile(gx, gz, mFM_GetReseveName(gx / UT_X_NUM + 1, gz / UT_Z_NUM + 1), "lot sign");
            }
        }
    }

    printf("[REPAIR] %d tiles repaired\n", s_repaired);
    if (s_repaired > 0) {
        snprintf(toast, sizeof(toast), "Town repaired: %d missing tiles put back.", s_repaired);
        pc_quicksave_toast_ms(toast, 10000); /* shown through the fade into town */
    }
    return s_repaired;
}
