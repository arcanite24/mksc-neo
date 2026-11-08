#ifndef PORTABLE_GP_ENTRY_H
#define PORTABLE_GP_ENTRY_H
#include <stddef.h>
#include <stdint.h>

enum { PORTABLE_GP_ENTRY_DRIVERS=8 };
typedef struct { int32_t x_q16,y_q16; } PortableGpEntryPoint;
typedef struct {
    PortableGpEntryPoint drivers[PORTABLE_GP_ENTRY_DRIVERS];
    int32_t camera_x_q16,camera_y_q16;
    uint16_t camera_yaw,panel_scroll;
    uint8_t veil_phase,window_top,window_bottom,hud_progress;
    uint8_t show_panel,show_hud,done;
} PortableGpEntrySnapshot;

typedef struct {
    PortableGpEntryPoint grid[PORTABLE_GP_ENTRY_DRIVERS];
    PortableGpEntrySnapshot snapshot;
    uint32_t ticks;
    uint16_t phase_ticks,camera_ticks;
    uint8_t phase,selected,grid_rank,first_course,camera_done,arrival_done,initialized;
} PortableGpEntry;

/* 52A4C presentation only: no race clocks, input, RNG or physics writes. */
int portable_gp_entry_init(PortableGpEntry *entry,const PortableGpEntryPoint grid[8],
    uint8_t selected,uint8_t grid_rank,uint8_t course_in_cup);
/* Source Q15 sine and vector angle. Returns -1 invalid,0 active,1 done. */
int portable_gp_entry_tick(PortableGpEntry *entry,const int32_t *sine,size_t sine_count);
#endif
