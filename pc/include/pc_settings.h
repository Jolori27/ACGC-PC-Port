#ifndef PC_SETTINGS_H
#define PC_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int window_width;
    int window_height;
    int fullscreen;       /* 0=windowed, 1=fullscreen, 2=borderless */
    int vsync;            /* 0=off, 1=on */
    int max_fps;          /* 0=uncapped, otherwise frame limiter target */
    int msaa;             /* 0=off, 2/4/8=samples */
    int texture_filtering; /* 0=force nearest-neighbor, 1=use the game's texture filtering */
    int preload_textures; /* 0=off (load on demand), 1=on (load all at startup), 2=on + cache file */
    int disable_resetti;  /* 0=normal (Resetti appears on reset), 1=disable reset penalty */
    int disable_shop_visitor_req; /* 0=normal (Nookington's needs a foreign-town shopper), 1=skip requirement */
    int borderless_acres; /* 0=original acre transitions (faster, draws less), 1=continuous camera/movement */
    int dash_speed;       /* Sprint (B held) speed as a percentage of vanilla, 100-200 (default 100) */
    int minimap;          /* Town minimap HUD: 0=hidden, 1=shown (default at launch; the Minimap key toggles it) */
    int minimap_size;     /* 0=small, 1=medium, 2=large */
    int minimap_corner;   /* 0=top right, 1=top left */
    int nes_aspect;       /* NES emulator aspect: 0=fullscreen stretch, 1=4:3 pillarbox (default) */
    int master_volume;    /* Applied at the PC audio output, 0-100 (default 100) */
    int stick_deadzone;   /* Gamepad main stick deadzone, percent 0-40 (default 12) */
    int cstick_deadzone;  /* Gamepad C-stick deadzone, percent 0-40 (default 12) */
    int quick_start;      /* New town: 0=normal intro, 1=skip train/Tom Nook intro/part-time job */
    int quick_start_gender; /* Quick start player gender: 0=boy, 1=girl */
    int quick_start_house;  /* Quick start house: 0=top-left, 1=top-right, 2=bottom-left, 3=bottom-right */
    char quick_start_player_name[32]; /* UTF-8, converted to the game's charset (max 8 characters) */
    char quick_start_town_name[32];   /* UTF-8, converted to the game's charset (max 8 characters) */
} PCSettings;

extern PCSettings g_pc_settings;

void pc_settings_load(void);
void pc_settings_save(void);
void pc_settings_apply(void);
void pc_settings_cycle_resolution(int* width, int* height, int dir);
int  pc_settings_encode_game_name(const char* utf8, unsigned char* dst, int max_len);

#ifdef __cplusplus
}
#endif

#endif /* PC_SETTINGS_H */
