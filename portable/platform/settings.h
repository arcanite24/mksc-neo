#ifndef PORTABLE_SETTINGS_H
#define PORTABLE_SETTINGS_H
#include <stddef.h>
#include <stdint.h>
#include "mod_manifest.h"
#include "../assets/asset_sources.h"

typedef enum {
    PORTABLE_SETTING_WINDOW_SCALE,
    PORTABLE_SETTING_FULLSCREEN,
    PORTABLE_SETTING_VSYNC,
    PORTABLE_SETTING_FILTER,
    PORTABLE_SETTING_VOLUME,
    PORTABLE_SETTING_DEADZONE,
    PORTABLE_SETTING_SHORT_TRANSITIONS,
    PORTABLE_SETTING_KEY_ACCELERATE, PORTABLE_SETTING_KEY_BRAKE,
    PORTABLE_SETTING_KEY_LEFT, PORTABLE_SETTING_KEY_RIGHT,
    PORTABLE_SETTING_KEY_HOP, PORTABLE_SETTING_KEY_ITEM,
    PORTABLE_SETTING_KEY_AIM_UP, PORTABLE_SETTING_KEY_AIM_DOWN,
    PORTABLE_SETTING_PAD_ACCELERATE, PORTABLE_SETTING_PAD_BRAKE,
    PORTABLE_SETTING_PAD_HOP, PORTABLE_SETTING_PAD_ITEM,
    PORTABLE_SETTING_INTERPOLATE, PORTABLE_SETTING_RENDER_FPS,
    PORTABLE_SETTING_INTERNAL_SCALE, PORTABLE_SETTING_FIT_SCALING,
    PORTABLE_SETTING_ASPECT_WIDTH,
    /* Second keyboard layout: drives player 2 in local VS and Battle. */
    PORTABLE_SETTING_KEY2_ACCELERATE, PORTABLE_SETTING_KEY2_BRAKE,
    PORTABLE_SETTING_KEY2_LEFT, PORTABLE_SETTING_KEY2_RIGHT,
    PORTABLE_SETTING_KEY2_HOP, PORTABLE_SETTING_KEY2_ITEM,
    PORTABLE_SETTING_KEY2_AIM_UP, PORTABLE_SETTING_KEY2_AIM_DOWN,
    /* Quality-of-life switches; every default keeps original behavior. */
    PORTABLE_SETTING_RUMBLE, PORTABLE_SETTING_INPUT_DISPLAY, PORTABLE_SETTING_MIRROR,
    PORTABLE_SETTING_FIX_DOPPLER_WRAP,
    PORTABLE_SETTING_WIDE_UI_BACKGROUNDS,
    PORTABLE_SETTING_DRIVING_PROFILE,
    /* 1 shows the original 240x160 menus instead of the modern ones. */
    PORTABLE_SETTING_CLASSIC_MENUS,
    PORTABLE_SETTING_TOUCH_CONTROLS,
    PORTABLE_SETTING_ARTWORK_LOD,
    PORTABLE_SETTING_RACE_RENDERER,
    /* 3D presentation effects; all on by default, off for low-power devices. */
    PORTABLE_SETTING_SMOOTH_3D, PORTABLE_SETTING_DYNAMIC_CAMERA, PORTABLE_SETTING_CAMERA_SHAKE,
    PORTABLE_SETTING_DRIFT_SPARKS, PORTABLE_SETTING_BOOST_EFFECTS, PORTABLE_SETTING_KART_TRAILS,
    PORTABLE_SETTING_IMMERSIVE_CAMERA, /* Optional mesh chase framing; off by default. */
    /* High-end GPU presentation, opt-in on every device. */
    PORTABLE_SETTING_HD2D_DOF, PORTABLE_SETTING_HD2D_BLOOM, PORTABLE_SETTING_HD2D_AO,
    PORTABLE_SETTING_HD2D_GRADE, PORTABLE_SETTING_HD2D_FOG, PORTABLE_SETTING_HD2D_LIGHT,
    PORTABLE_SETTING_HD2D_SCALE,
    PORTABLE_SETTING_COUNT
} PortableSetting;

typedef struct {
    uint8_t window_scale,fullscreen,vsync,linear_filter,master_volume;
    uint8_t short_transitions;
    uint16_t steering_deadzone,render_fps,aspect_width;
    uint8_t interpolate,mod_count;
    uint8_t internal_scale,fit_scaling;
    uint8_t rumble,input_display,mirror,fix_doppler_wrap;
    uint8_t wide_ui_backgrounds;
    uint8_t driving_profile; /* Drift extras: 0 Off, 1 Tiers, 2 Tiers + Air Boost; stable save domain. */
    uint8_t classic_menus;
    uint8_t touch_controls; /* Optional on-screen controls; off on every host. */
    uint8_t artwork_lod; /* Presentation only: 0 Original, 1 High, 2 Max. */
    uint8_t race_renderer; /* Presentation only: 0 Original, 1 3D. */
    uint8_t smooth_3d, dynamic_camera, camera_shake; /* Presentation only. */
    uint8_t drift_sparks, boost_effects, kart_trails;
    uint8_t immersive_camera; /* Presentation only; keeps the MK8 camera when off. */
    uint8_t hd2d_dof, hd2d_bloom, hd2d_ao, hd2d_grade, hd2d_fog, hd2d_light, hd2d_scale;
    uint8_t store_url_count;
    char store_urls[4][4096]; /* HTTPS or absolute file URLs, at most 1023 bytes. */
    uint8_t mod_directory_count;
    char mod_directories[4][4096]; /* Remembered extras; primary mods directory stays caller-owned. */
    char mods[PORTABLE_MOD_LIMIT][PORTABLE_MOD_ID_CAPACITY];
    char asset_sources[PORTABLE_ASSET_SOURCE_COUNT][PORTABLE_MOD_ID_CAPACITY];
    uint16_t keys[16]; /* SDL USB scancodes: 0-7 player 1, 8-15 player 2. */
    uint8_t pad_buttons[4]; /* SDL standard gamepad buttons; system buttons reserved. */
} PortableSettings;

typedef struct {
    PortableSettings values;
    uint64_t mask;
    const char *mods_dir;
    uint8_t no_mods,mods_set,watch_mods,watch_mods_set,asset_sources_set;
} PortableSettingsOverrides;

/* Preset identity is derived so individual edits reliably show Custom. */
unsigned portable_settings_hd2d_preset(const PortableSettings *settings);
void portable_settings_hd2d(PortableSettings *settings, int enabled);

#define PORTABLE_SETTINGS_ERROR_CAPACITY 96u
void portable_settings_defaults(PortableSettings *settings);
const char *portable_setting_name(PortableSetting field);
uint32_t portable_setting_get(const PortableSettings *settings,PortableSetting field);
int portable_setting_set(PortableSettings *settings,PortableSetting field,uint32_t value);
/* Version-1 JSON (scalar settings plus an ordered mods array); omitted fields use defaults. Reject unknown/duplicate
 * fields and invalid syntax/ranges as one transaction, returning defaults. */
int portable_settings_parse(const char *json,size_t size,PortableSettings *settings,char *error);
/* 1 loaded, 0 absent, -1 invalid/unreadable; all failures return defaults. */
int portable_settings_load(const char *path,PortableSettings *settings,char *error);
int portable_settings_save(const char *path,const PortableSettings *settings);
int portable_settings_cli(int argc,char **argv,PortableSettingsOverrides *overrides,char *error);
void portable_settings_apply(PortableSettings *settings,const PortableSettingsOverrides *overrides);
#endif
