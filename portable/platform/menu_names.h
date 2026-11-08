#ifndef PORTABLE_MENU_NAMES_H
#define PORTABLE_MENU_NAMES_H

#include "asset_pack.h"
#include <stddef.h>
#include <stdint.h>

/* Display names for the modern menus from portable/data/menu-names.json,
 * embedded at build time. Content the table does not list (roster drivers,
 * custom courses) has no text name; callers then show the pack's name art. */
const char *portable_menu_driver_name(uint32_t driver_id);
const char *portable_menu_course_name(uint32_t course_id);
const char *portable_menu_cup_name(unsigned cup);
/* Speed and weight ratings 1..5; 0 when the driver has none listed. */
int portable_menu_driver_stats(uint32_t driver_id, unsigned *speed, unsigned *weight);
/* A 3D kart's name and speed, acceleration and handling ratings 1..5 among
 * the listed karts; NULL for an unknown kart. */
const char *portable_menu_kart(uint32_t kart_id, unsigned ratings[3]);
/* Also names (and rates from their physics ratios) the 3D pack's kind-110
 * drivers the table lacks, and lists its kind-111 karts; NULL forgets them. */
void portable_menu_names_set_3d_drivers(const PortableAssetPackView *pack_3d);
/* Parses a names document into the process table; 1 on success. Exposed for
 * tests; the embedded table loads on first use. */
int portable_menu_names_parse(const char *json, size_t size);

#endif
