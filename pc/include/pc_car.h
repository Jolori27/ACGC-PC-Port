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
 * The seat anchor is the player's position while driving. The riding pose
 * (mPlayer_ANIM_RIDEWAIT) puts the hips 2096 up and 1700 back from it, and
 * the car is drawn PC_CAR_WHEEL_BOTTOM higher, so the hips end up at
 * (SEAT_X, SEAT_Y + 1752, SEAT_Z - 1700): here on the two bucket seats'
 * cushion (Y 1500..1875 over Z -1312..+1125), close to the windshield. */
#define PC_CAR_SEAT_X 0.0f
#define PC_CAR_SEAT_Y 600.0f
#define PC_CAR_SEAT_Z 950.0f

/* Car body box in car-aligned model units (from the vertex data). */
#define PC_CAR_BOX_X_MIN (-3750.0f)
#define PC_CAR_BOX_X_MAX 3750.0f
#define PC_CAR_BOX_Z_MIN (-6001.0f)
#define PC_CAR_BOX_Z_MAX 6187.0f
/* The wheels reach below the model origin. */
#define PC_CAR_WHEEL_BOTTOM 544.0f

/* Vehicle types, cycled with F5. */
enum {
    PC_VEHICLE_CAR,
    PC_VEHICLE_SCOOTER_PIZZA,
    PC_VEHICLE_SCOOTER_MASCUILO,
    PC_VEHICLE_SCOOTER_FEMINTINE,
    PC_VEHICLE_NUM
};

typedef struct {
    const char* name;
    f32 scale;        /* model units -> world */
    f32 seat_x;       /* anchor in model units (the player's position while driving) */
    f32 seat_y;
    f32 seat_z;
    f32 lift_y;       /* model units the body is raised so the wheels touch the ground */
    f32 box_x_min;    /* body box, model units */
    f32 box_x_max;
    f32 box_z_min;
    f32 box_z_max;
    f32 top_speed;    /* world units per frame */
    s16 roll_max;     /* body roll in bends */
    int lean_in;      /* leans into bends (two wheels) rather than out */
    f32 probe_radius; /* wall probes along the centre line, world units */
    int probe_num;
    f32 wheel_radius; /* world units */
    f32 engine_pitch; /* engine sound pitch factor */
} PcVehicleDef;

typedef struct {
    int active;   /* the vehicle exists in the town */
    int type;     /* PC_VEHICLE_* */
    int driving;  /* the player is seated in it (mPlayer_INDEX_PC_DRIVE) */
    int ghost;    /* the player left it without hopping out: no push until clear */
    int seated;   /* spawn around the player: skip the jump in, just sit down */
    xyz_t pos;    /* seat anchor on the ground (player root while driving) */
    s16 angle;    /* heading, actor convention: moves along (sin, cos) */
    /* animation state, written by the drive state each frame */
    s16 wheel;    /* wheel spin angle */
    s16 steer;    /* steering angle (+ = left) */
    s16 roll;     /* body roll about the forward axis */
    s16 pitch;    /* body pitch about the side axis */
    f32 bob;      /* engine shake, world units */
} PcCar;

/* Definition of the current vehicle type. */
const PcVehicleDef* pc_car_def(void);

extern PcCar g_pc_car;

/* F5: ask the game thread to pop the car in front of the player, switch
 * to the next vehicle, or remove it. Consumed once per press by the
 * player actor. */
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

/* Draws the vehicle at g_pc_car.pos / angle. Called from the player draw,
 * inside the actor draw pass. */
void pc_car_draw(struct game_s* game);

#ifdef __cplusplus
}
#endif

#endif /* PC_CAR_H */
