#ifndef PORTABLE_MENU_DRIVER_MODEL_H
#define PORTABLE_MENU_DRIVER_MODEL_H

#include "asset_pack.h"
#include "software_frame.h"
#include "ui_canvas.h"

/* Character-select preview of a 3D-domain driver: its rigged driver model
 * seated in a kind-111 kart, posed and turned on a transparent square, drawn
 * by the CPU scene renderer from the same model content as the race. Menus
 * fall back to the driver's emblem or stock art when this returns 0. No SDL;
 * presentation only. */
typedef struct PortableMenuDriverModel PortableMenuDriverModel;

PortableMenuDriverModel *portable_menu_driver_model_create(void);
void portable_menu_driver_model_destroy(PortableMenuDriverModel *model);
/* Drops every resource borrowed from the current pack. Call before a pack is
 * retired, so a replacement at the same address cannot reuse stale content. */
void portable_menu_driver_model_forget(PortableMenuDriverModel *model);

typedef struct
{
    uint32_t driver_id;
    uint32_t kart_id;  /* 0: the kart the driver races by default. */
    unsigned size;     /* Output edge in pixels, 8..1024. */
    double yaw;        /* Radians; 0 faces the camera. */
    uint32_t tick;     /* Rig and material clock (menu ticks). */
    uint8_t celebrate; /* Play the win clip from tick 0 instead of driving. */
} PortableMenuDriverModelPose;

/* Renders into an owned straight-alpha image valid until the next render,
 * forget or destroy. 0 when pack has no model, seat or kart for the driver;
 * each failing driver/kart pair is reported once and not retried. */
int portable_menu_driver_model_render(PortableMenuDriverModel *model, const PortableAssetPackView *pack,
                                      const PortableMenuDriverModelPose *pose, UiImage *out);

/* The same render composited over a software frame with its physical scale:
 * centred at native (center_x, center_y), size native pixels wide. */
int portable_menu_driver_model_draw_frame(PortableMenuDriverModel *model, const PortableAssetPackView *pack,
                                          const PortableMenuDriverModelPose *pose, PortableSoftwareFrame *frame,
                                          int center_x, int center_y);

/* A kind-110 driver's own portrait fitted (aspect kept, centred) into the
 * native rectangle of a software frame, over an opaque backdrop when its
 * alpha is non-zero. 0 when the pack has no usable portrait. */
int portable_menu_driver_portrait_draw_frame(const PortableAssetPackView *pack, uint32_t driver_id,
                                             PortableSoftwareFrame *frame, int left, int top, int width,
                                             int height, uint32_t backdrop);

#endif
