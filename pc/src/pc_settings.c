/* pc_settings.c - runtime settings loaded from settings.ini */
#include "pc_settings.h"
#include "pc_platform.h"
#include "m_player_lib.h"
#include "ac_birth_control.h"
#include "m_font.h"
#include "pc_typing.h"

PCSettings g_pc_settings = {
    .window_width  = PC_SCREEN_WIDTH,
    .window_height = PC_SCREEN_HEIGHT,
    .fullscreen    = 0,
    .vsync         = 0,
    .max_fps       = 60,
    .msaa          = 4,
    .texture_filtering = 1,
    .preload_textures = 0,
    .disable_resetti = 0,
    .disable_shop_visitor_req = 0,
    .borderless_acres = 1,
    .dash_speed = 100,
    .minimap = 0,
    .minimap_size = 1,
    .minimap_corner = 0,
    .nes_aspect = 1,
    .master_volume = 100,
    .stick_deadzone = 12,
    .cstick_deadzone = 12,
    .quick_start = 0,
    .quick_start_gender = 0,
    .quick_start_house = 0,
    .quick_start_player_name = "Player",
    .quick_start_town_name = "Town",
};

static const char* SETTINGS_FILE = "settings.ini";

static const char* DEFAULT_SETTINGS =
    "[Graphics]\n"
    "# Window size (ignored in fullscreen)\n"
    "window_width = 640\n"
    "window_height = 480\n"
    "\n"
    "# 0 = windowed, 1 = fullscreen, 2 = borderless fullscreen\n"
    "fullscreen = 0\n"
    "\n"
    "# Vertical sync: 0 = off, 1 = on\n"
    "vsync = 0\n"
    "\n"
    "# Max FPS: 60, 120, 240, or 0 for uncapped\n"
    "max_fps = 60\n"
    "\n"
    "# Anti-aliasing samples: 0 = off, 2, 4, or 8\n"
    "msaa = 4\n"
    "\n"
    "# Texture filtering: 0 = nearest-neighbor, 1 = use the game's filtering\n"
    "texture_filtering = 1\n"
    "\n"
    "[Enhancements]\n"
    "# Preload HD textures at startup: 0 = off (load on demand), 1 = preload, 2 = preload + cache file (fastest)\n"
    "preload_textures = 0\n"
    "\n"
    "[Gameplay]\n"
    "# Disable Mr. Resetti: 0 = normal, 1 = disable\n"
    "disable_resetti = 0\n"
    "\n"
    "# Shop upgrade visitor requirement (Nookington's needs a shopper from another town): 0 = required, 1 = not required\n"
    "disable_shop_visitor_req = 0\n"
    "\n"
    "# Borderless acres: 0 = original acre transitions (faster, draws less), 1 = continuous movement/camera\n"
    "borderless_acres = 1\n"
    "\n"
    "# Sprint speed (holding B) as a percentage of the original: 100-200\n"
    "dash_speed = 100\n"
    "\n"
    "# Town minimap (toggle in game with the Minimap key, Tab / LB by default): 0 = hidden, 1 = shown\n"
    "minimap = 0\n"
    "\n"
    "# Minimap size: 0 = small, 1 = medium, 2 = large\n"
    "minimap_size = 1\n"
    "\n"
    "# Minimap corner: 0 = top right, 1 = top left\n"
    "minimap_corner = 0\n"
    "\n"
    "# NES emulator aspect ratio: 0 = stretch to fullscreen, 1 = 4:3 pillarbox\n"
    "nes_aspect = 1\n"
    "\n"
    "[QuickStart]\n"
    "# Skip the intro when starting a new town: 0 = normal intro, 1 = skip\n"
    "# (train ride with Rover, Tom Nook's house sale and the part-time job)\n"
    "quick_start = 0\n"
    "\n"
    "# Names used by the quick start (max 8 characters each)\n"
    "quick_start_player_name = Player\n"
    "quick_start_town_name = Town\n"
    "\n"
    "# 0 = boy, 1 = girl\n"
    "quick_start_gender = 0\n"
    "\n"
    "# House: 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right\n"
    "quick_start_house = 0\n"
    "\n"
    "[Audio]\n"
    "# Master output volume as a percentage (0-100)\n"
    "master_volume = 100\n"
    "\n"
    "[Input]\n"
    "# Gamepad stick deadzones as a percentage (0-40)\n"
    "stick_deadzone = 12\n"
    "cstick_deadzone = 12\n";

static const char* skip_ws(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void trim_end(char* s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                       s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
}

static void copy_string_setting(char* dst, size_t size, const char* value) {
    strncpy(dst, value, size - 1);
    dst[size - 1] = '\0';
}

static void apply_setting(const char* key, const char* value) {
    int val = atoi(value);

    if (strcmp(key, "quick_start_player_name") == 0) {
        copy_string_setting(g_pc_settings.quick_start_player_name,
                            sizeof(g_pc_settings.quick_start_player_name), value);
    } else if (strcmp(key, "quick_start_town_name") == 0) {
        copy_string_setting(g_pc_settings.quick_start_town_name,
                            sizeof(g_pc_settings.quick_start_town_name), value);
    } else if (strcmp(key, "window_width") == 0) {
        if (val >= 640) g_pc_settings.window_width = val;
    } else if (strcmp(key, "window_height") == 0) {
        if (val >= 480) g_pc_settings.window_height = val;
    } else if (strcmp(key, "fullscreen") == 0) {
        if (val >= 0 && val <= 2) g_pc_settings.fullscreen = val;
    } else if (strcmp(key, "vsync") == 0) {
        if (val == 0 || val == 1) g_pc_settings.vsync = val;
    } else if (strcmp(key, "max_fps") == 0) {
        if (val >= 0) {
            g_pc_settings.max_fps = val;
        }
    } else if (strcmp(key, "msaa") == 0) {
        if (val == 0 || val == 2 || val == 4 || val == 8)
            g_pc_settings.msaa = val;
    } else if (strcmp(key, "texture_filtering") == 0) {
        if (val == 0 || val == 1) g_pc_settings.texture_filtering = val;
    } else if (strcmp(key, "preload_textures") == 0) {
        if (val >= 0 && val <= 2) g_pc_settings.preload_textures = val;
    } else if (strcmp(key, "disable_resetti") == 0) {
        if (val == 0 || val == 1) g_pc_settings.disable_resetti = val;
    } else if (strcmp(key, "disable_shop_visitor_req") == 0) {
        if (val == 0 || val == 1) g_pc_settings.disable_shop_visitor_req = val;
    } else if (strcmp(key, "borderless_acres") == 0) {
        if (val == 0 || val == 1) g_pc_settings.borderless_acres = val;
    } else if (strcmp(key, "dash_speed") == 0) {
        if (val >= 100 && val <= 200) g_pc_settings.dash_speed = val;
    } else if (strcmp(key, "minimap") == 0) {
        if (val == 0 || val == 1) g_pc_settings.minimap = val;
    } else if (strcmp(key, "minimap_size") == 0) {
        if (val >= 0 && val <= 2) g_pc_settings.minimap_size = val;
    } else if (strcmp(key, "minimap_corner") == 0) {
        if (val == 0 || val == 1) g_pc_settings.minimap_corner = val;
    } else if (strcmp(key, "nes_aspect") == 0) {
        if (val == 0 || val == 1) g_pc_settings.nes_aspect = val;
    } else if (strcmp(key, "master_volume") == 0) {
        if (val >= 0 && val <= 100) g_pc_settings.master_volume = val;
    } else if (strcmp(key, "stick_deadzone") == 0) {
        if (val >= 0 && val <= 40) g_pc_settings.stick_deadzone = val;
    } else if (strcmp(key, "cstick_deadzone") == 0) {
        if (val >= 0 && val <= 40) g_pc_settings.cstick_deadzone = val;
    } else if (strcmp(key, "quick_start") == 0) {
        if (val == 0 || val == 1) g_pc_settings.quick_start = val;
    } else if (strcmp(key, "quick_start_gender") == 0) {
        if (val == 0 || val == 1) g_pc_settings.quick_start_gender = val;
    } else if (strcmp(key, "quick_start_house") == 0) {
        if (val >= 0 && val <= 3) g_pc_settings.quick_start_house = val;
    }
}

static void apply_frame_limit_setting(void) {
    int max_fps;

    if (g_pc_frame_limit_override >= 0) {
        g_pc_settings.max_fps = g_pc_frame_limit_override;
        g_pc_frame_limit_override = -1;
    }

    max_fps = g_pc_settings.max_fps;

    if (max_fps <= 0) {
        max_fps = 0;
    }

    g_frame_limiter = (u32)max_fps;
}

static void apply_borderless_acres_setting(void) {
    int enabled = g_pc_settings.borderless_acres != 0;

    if (enabled && !g_mPlib_wade_disabled) {
        aBC_RequestNearbyRefresh();
    }
    g_mPlib_wade_disabled = enabled;
}

static void write_defaults(const char* path) {
    FILE* f = fopen(path, "w");
    if (f) {
        fputs(DEFAULT_SETTINGS, f);
        fclose(f);
    }
}

void pc_settings_save(void) {
    FILE* f = fopen(SETTINGS_FILE, "w");
    if (!f) {
        printf("[Settings] Failed to write %s\n", SETTINGS_FILE);
        return;
    }
    fprintf(f, "[Graphics]\n");
    fprintf(f, "# Window size (ignored in fullscreen)\n");
    fprintf(f, "window_width = %d\n", g_pc_settings.window_width);
    fprintf(f, "window_height = %d\n", g_pc_settings.window_height);
    fprintf(f, "\n");
    fprintf(f, "# 0 = windowed, 1 = fullscreen, 2 = borderless fullscreen\n");
    fprintf(f, "fullscreen = %d\n", g_pc_settings.fullscreen);
    fprintf(f, "\n");
    fprintf(f, "# Vertical sync: 0 = off, 1 = on\n");
    fprintf(f, "vsync = %d\n", g_pc_settings.vsync);
    fprintf(f, "\n");
    fprintf(f, "# Max FPS: 60, 120, 240, or 0 for uncapped\n");
    fprintf(f, "max_fps = %d\n", g_pc_settings.max_fps);
    fprintf(f, "\n");
    fprintf(f, "# Anti-aliasing samples: 0 = off, 2, 4, or 8\n");
    fprintf(f, "msaa = %d\n", g_pc_settings.msaa);
    fprintf(f, "\n");
    fprintf(f, "# Texture filtering: 0 = nearest-neighbor, 1 = use the game's filtering\n");
    fprintf(f, "texture_filtering = %d\n", g_pc_settings.texture_filtering);
    fprintf(f, "\n");
    fprintf(f, "[Enhancements]\n");
    fprintf(f, "# Preload HD textures at startup: 0 = off (load on demand), 1 = preload, 2 = preload + cache file (fastest)\n");
    fprintf(f, "preload_textures = %d\n", g_pc_settings.preload_textures);
    fprintf(f, "\n");
    fprintf(f, "[Gameplay]\n");
    fprintf(f, "# Disable Mr. Resetti: 0 = normal, 1 = disable\n");
    fprintf(f, "disable_resetti = %d\n", g_pc_settings.disable_resetti);
    fprintf(f, "\n");
    fprintf(f, "# Shop upgrade visitor requirement (Nookington's needs a shopper from another town): 0 = required, 1 = not required\n");
    fprintf(f, "disable_shop_visitor_req = %d\n", g_pc_settings.disable_shop_visitor_req);
    fprintf(f, "\n");
    fprintf(f, "# Borderless acres: 0 = original acre transitions (faster, draws less), 1 = continuous movement/camera\n");
    fprintf(f, "borderless_acres = %d\n", g_pc_settings.borderless_acres);
    fprintf(f, "\n");
    fprintf(f, "# Sprint speed (holding B) as a percentage of the original: 100-200\n");
    fprintf(f, "dash_speed = %d\n", g_pc_settings.dash_speed);
    fprintf(f, "\n");
    fprintf(f, "# Town minimap (toggle in game with the Minimap key, Tab / LB by default): 0 = hidden, 1 = shown\n");
    fprintf(f, "minimap = %d\n", g_pc_settings.minimap);
    fprintf(f, "\n");
    fprintf(f, "# Minimap size: 0 = small, 1 = medium, 2 = large\n");
    fprintf(f, "minimap_size = %d\n", g_pc_settings.minimap_size);
    fprintf(f, "\n");
    fprintf(f, "# Minimap corner: 0 = top right, 1 = top left\n");
    fprintf(f, "minimap_corner = %d\n", g_pc_settings.minimap_corner);
    fprintf(f, "\n");
    fprintf(f, "# NES emulator aspect ratio: 0 = stretch to fullscreen, 1 = 4:3 pillarbox\n");
    fprintf(f, "nes_aspect = %d\n", g_pc_settings.nes_aspect);
    fprintf(f, "\n");
    fprintf(f, "[QuickStart]\n");
    fprintf(f, "# Skip the intro when starting a new town: 0 = normal intro, 1 = skip\n");
    fprintf(f, "# (train ride with Rover, Tom Nook's house sale and the part-time job)\n");
    fprintf(f, "quick_start = %d\n", g_pc_settings.quick_start);
    fprintf(f, "\n");
    fprintf(f, "# Names used by the quick start (max 8 characters each)\n");
    fprintf(f, "quick_start_player_name = %s\n", g_pc_settings.quick_start_player_name);
    fprintf(f, "quick_start_town_name = %s\n", g_pc_settings.quick_start_town_name);
    fprintf(f, "\n");
    fprintf(f, "# 0 = boy, 1 = girl\n");
    fprintf(f, "quick_start_gender = %d\n", g_pc_settings.quick_start_gender);
    fprintf(f, "\n");
    fprintf(f, "# House: 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right\n");
    fprintf(f, "quick_start_house = %d\n", g_pc_settings.quick_start_house);
    fprintf(f, "\n");
    fprintf(f, "[Audio]\n");
    fprintf(f, "# Master output volume as a percentage (0-100)\n");
    fprintf(f, "master_volume = %d\n", g_pc_settings.master_volume);
    fprintf(f, "\n");
    fprintf(f, "[Input]\n");
    fprintf(f, "# Gamepad stick deadzones as a percentage (0-40)\n");
    fprintf(f, "stick_deadzone = %d\n", g_pc_settings.stick_deadzone);
    fprintf(f, "cstick_deadzone = %d\n", g_pc_settings.cstick_deadzone);
    fclose(f);
    printf("[Settings] Saved %s\n", SETTINGS_FILE);
}

/* Convert a UTF-8 name from settings.ini to the game's character set,
 * space-padded to max_len like the in-game name editor leaves it.
 * Characters the game font can't show are dropped. Returns the number
 * of non-space characters written (0 = unusable name). */
int pc_settings_encode_game_name(const char* utf8, unsigned char* dst, int max_len) {
    const unsigned char* p = (const unsigned char*)utf8;
    int len = 0;
    int visible = 0;

    while (*p != '\0' && len < max_len) {
        int bytes = (*p < 0x80) ? 1 : ((*p & 0xE0) == 0xC0) ? 2 : ((*p & 0xF0) == 0xE0) ? 3 : 4;
        int code;

#ifdef KEYBOARD_TYPING
        if (*p >= 0x80 && (p[1] & 0xC0) != 0x80) {
            /* Not UTF-8: the file was saved as ANSI/Latin-1 (e.g. old Notepad). */
            char latin1[3] = { (char)(0xC0 | (*p >> 6)), (char)(0x80 | (*p & 0x3F)), '\0' };
            code = pc_utf8_to_game_code(latin1);
            bytes = 1;
        } else {
            code = pc_utf8_to_game_code((const char*)p);
        }
#else
        code = (*p >= 0x20 && *p < 0x7F) ? *p : -1;
#endif
        if (code >= 0) {
            dst[len++] = (unsigned char)code;
            if (code != CHAR_SPACE) visible++;
        }

        while (bytes-- > 0 && *p != '\0') p++;
    }

    while (len < max_len) {
        dst[len++] = CHAR_SPACE;
    }

    return visible;
}

/* Accessor for TUs that can't include pc_settings.h (pc_nes_fixnes.c). */
int pc_settings_get_nes_aspect(void) {
    return g_pc_settings.nes_aspect;
}

/* --- Resolution preset table (shared) ---
 * Ordered by width then height. The desktop's native size is injected at
 * first use (de-duplicated against the static list) so the user can snap
 * to whatever their monitor is running. Multiple presets share widths now
 * (e.g. 1280x720 vs 1280x960), so cycling is index-based rather than the
 * old width-comparison. */
#define RES_MAX 32
static int res_w_tbl[RES_MAX];
static int res_h_tbl[RES_MAX];
static int res_count = 0;

static void add_preset(int w, int h) {
    if (res_count >= RES_MAX) return;
    for (int i = 0; i < res_count; i++) {
        if (res_w_tbl[i] == w && res_h_tbl[i] == h) return; /* de-dupe */
    }
    int at = res_count;
    for (int i = 0; i < res_count; i++) {
        if (res_w_tbl[i] > w || (res_w_tbl[i] == w && res_h_tbl[i] > h)) {
            at = i;
            break;
        }
    }
    for (int i = res_count; i > at; i--) {
        res_w_tbl[i] = res_w_tbl[i - 1];
        res_h_tbl[i] = res_h_tbl[i - 1];
    }
    res_w_tbl[at] = w;
    res_h_tbl[at] = h;
    res_count++;
}

static void ensure_presets(void) {
    if (res_count > 0) return;
    /* 4:3 */
    add_preset(640,  480);
    add_preset(800,  600);
    add_preset(960,  720);
    add_preset(1024, 768);
    add_preset(1152, 864);
    add_preset(1280, 960);
    add_preset(1400, 1050);
    add_preset(1600, 1200);
    /* 16:9 */
    add_preset(1280, 720);
    add_preset(1366, 768);
    add_preset(1600, 900);
    add_preset(1920, 1080);
    add_preset(2560, 1440);
    add_preset(3840, 2160);
    /* Desktop native (inserted sorted, de-duped against the list above). */
    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(0, &mode) == 0) {
        add_preset(mode.w, mode.h);
    }
}

/* Find the closest preset by total pixel count. Used when the caller's
 * current (w, h) isn't in the list (e.g. custom settings.ini value). */
static int nearest_index(int w, int h) {
    long long want = (long long)w * h;
    int best = 0;
    long long best_diff = (long long)1 << 62;
    for (int i = 0; i < res_count; i++) {
        long long diff = (long long)res_w_tbl[i] * res_h_tbl[i] - want;
        if (diff < 0) diff = -diff;
        if (diff < best_diff) { best_diff = diff; best = i; }
    }
    return best;
}

void pc_settings_cycle_resolution(int* width, int* height, int dir) {
    ensure_presets();
    int cur = -1;
    for (int i = 0; i < res_count; i++) {
        if (res_w_tbl[i] == *width && res_h_tbl[i] == *height) { cur = i; break; }
    }
    if (cur < 0) cur = nearest_index(*width, *height);

    if (dir > 0 && cur < res_count - 1) cur++;
    else if (dir < 0 && cur > 0) cur--;

    *width  = res_w_tbl[cur];
    *height = res_h_tbl[cur];
}

void pc_settings_apply(void) {
    apply_frame_limit_setting();
    apply_borderless_acres_setting();

    if (!g_pc_window) return;

    int w = g_pc_settings.window_width;
    int h = g_pc_settings.window_height;

    switch (g_pc_settings.fullscreen) {
        case 1: {
            /* Exclusive fullscreen at the user's chosen resolution. SDL
             * needs the display mode set before transitioning to
             * SDL_WINDOW_FULLSCREEN or it'll just use the desktop mode. */
            SDL_DisplayMode target = { 0 };
            target.w = w;
            target.h = h;
            int display_idx = SDL_GetWindowDisplayIndex(g_pc_window);
            if (display_idx < 0) display_idx = 0;
            SDL_DisplayMode closest;
            if (SDL_GetClosestDisplayMode(display_idx, &target, &closest)) {
                SDL_SetWindowDisplayMode(g_pc_window, &closest);
            }
            SDL_SetWindowFullscreen(g_pc_window, SDL_WINDOW_FULLSCREEN);
            break;
        }
        case 2: {
            /* Borderless window at the user's chosen size, centred. Exit
             * any fullscreen mode first (including FULLSCREEN_DESKTOP)
             * so the resize sticks. */
            SDL_SetWindowFullscreen(g_pc_window, 0);
            SDL_SetWindowBordered(g_pc_window, SDL_FALSE);
            SDL_SetWindowSize(g_pc_window, w, h);
            SDL_SetWindowPosition(g_pc_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            break;
        }
        case 0:
        default: {
            SDL_SetWindowFullscreen(g_pc_window, 0);
            SDL_SetWindowBordered(g_pc_window, SDL_TRUE);
            SDL_SetWindowSize(g_pc_window, w, h);
            SDL_SetWindowPosition(g_pc_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            break;
        }
    }

    SDL_GL_SetSwapInterval(g_pc_settings.vsync);
    pc_platform_update_window_size();

    printf("[Settings] Applied: %dx%d fullscreen=%d vsync=%d max_fps=%d msaa=%d\n",
           g_pc_settings.window_width, g_pc_settings.window_height,
           g_pc_settings.fullscreen, g_pc_settings.vsync, g_pc_settings.max_fps, g_pc_settings.msaa);
}

void pc_settings_load(void) {
    FILE* f = fopen(SETTINGS_FILE, "r");
    if (!f) {
        write_defaults(SETTINGS_FILE);
        apply_frame_limit_setting();
        apply_borderless_acres_setting();
        printf("[Settings] Created default %s\n", SETTINGS_FILE);
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        const char* p = skip_ws(line);

        if (*p == '#' || *p == ';' || *p == '\0' || *p == '\n' || *p == '[')
            continue;

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = (char*)skip_ws(line);
        trim_end(key);
        char* value = (char*)skip_ws(eq + 1);
        trim_end(value);

        if (*key && *value) {
            apply_setting(key, value);
        }
    }
    fclose(f);
    apply_frame_limit_setting();
    apply_borderless_acres_setting();

    printf("[Settings] Loaded %s: %dx%d fullscreen=%d vsync=%d max_fps=%d msaa=%d preload_textures=%d borderless_acres=%d\n",
           SETTINGS_FILE, g_pc_settings.window_width, g_pc_settings.window_height,
           g_pc_settings.fullscreen, g_pc_settings.vsync, g_pc_settings.max_fps, g_pc_settings.msaa,
           g_pc_settings.preload_textures, g_pc_settings.borderless_acres);
}
