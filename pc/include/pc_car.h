/* pc_car.h - F5 debug car: Gracie's car from the designer event, drivable */
#ifndef PC_CAR_H
#define PC_CAR_H

#include "types.h"
#include "m_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;

/* World scale of the car model (the player is drawn at 0.01). */
#define PC_CAR_SCALE 0.01f

/* Car-aligned model space: the model is authored rotated 45 degrees; after
 * undoing that, +Z is the hood and +X the side that has wheels modelled.
 * The seat is where the player's root sits while driving: centred on the
 * two bucket seats' cushion (Y 1500..1875 over Z -1312..+1125), in front of
 * the reclined backrest that rises behind it to Z -4875. */
#define PC_CAR_SEAT_X 0.0f
#define PC_CAR_SEAT_Y 2000.0f
#define PC_CAR_SEAT_Z (-450.0f)

/* Car body box in car-aligned model units (from the vertex data). */
#define PC_CAR_BOX_X_MIN (-3750.0f)
#define PC_CAR_BOX_X_MAX 3750.0f
#define PC_CAR_BOX_Z_MIN (-6001.0f)
#define PC_CAR_BOX_Z_MAX 6187.0f
/* The wheels reach below the model origin. */
#define PC_CAR_WHEEL_BOTTOM 544.0f

typedef struct {
    int active;  /* the car exists in the town */
    int driving; /* the player is seated in it (mPlayer_INDEX_PC_DRIVE) */
    int ghost;   /* the player left it without hopping out: no push until clear */
    xyz_t pos;   /* seat anchor on the ground (player root while driving) */
    s16 angle;   /* heading, actor convention: moves along (sin, cos) */
} PcCar;

extern PcCar g_pc_car;

/* F5: ask the game thread to pop the car in front of the player, or to
 * remove it. Consumed once per press by the player actor. */
void pc_car_request_toggle(void);
int pc_car_take_toggle_request(void);

/* FALSE while a text field, the settings menu or a non-game screen owns
 * the keyboard. */
int pc_car_toggle_allowed(void);

/* Player position relative to the parked car, in world units along the
 * car's side (x) and forward (z) axes from the seat anchor. */
void pc_car_world_to_local(const xyz_t* pos, f32* lx, f32* lz);
void pc_car_local_to_world(f32 lx, f32 lz, xyz_t* pos);

/* Box edges in world units relative to the seat anchor. */
f32 pc_car_box_x_min(void);
f32 pc_car_box_x_max(void);
f32 pc_car_box_z_min(void);
f32 pc_car_box_z_max(void);

/* Draws the car at g_pc_car.pos / angle (both halves and its shadow).
 * Called from the player draw, inside the actor draw pass. */
void pc_car_draw(struct game_s* game);

#ifdef __cplusplus
}
#endif

#endif /* PC_CAR_H */
