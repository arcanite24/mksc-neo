#ifndef MKSC_PORTABLE_DRIVER_SPRITE_ASSET_H
#define MKSC_PORTABLE_DRIVER_SPRITE_ASSET_H

#include <stddef.h>
#include <stdint.h>

#include "asset_pack.h"

/* Kind 113 KSP1 (content ID = driver ID): a driver drawn as directional
 * sprite frames, as Mario Kart 64 draws its racers. 32-byte header (magic,
 * u16 width/height <= 128, frames, palette phases 1..4, shared colours
 * <= 256, Q16 metres per pixel, Q16 metres driven per phase step, then four
 * bytes that are zero in version 1). Version 1 (<= 64 frames) follows with
 * 64 slot bytes naming the frame seen from each half-circle view bin (bin 0
 * from behind, the other half mirrors). Version 2 (<= 512 frames) uses the
 * four bytes as pitch rows 1..16, tumble frames 0..64, tumble cycles and
 * flags, then 32 effect bytes (four additive star colours, ticks per star
 * colour, Time Trial ghost alpha, Boo alpha, 13 zero), the rows' lowest
 * pitches (i16 binary angles, nose up positive, ascending from -32768), the
 * rows' 64 u16 slots, the tumble frames (u16), each list padded to 4 bytes.
 * Both then hold the shared ARGB colours and, per frame, width*height
 * colour indices and, per phase, 256 - shared ARGB colours for the indices
 * past the shared ones (wheel colours that turn). */
#define PORTABLE_DRIVER_SPRITE_MAGIC UINT32_C(0x3150534B)
#define PORTABLE_DRIVER_SPRITE_FRAMES 512u
#define PORTABLE_DRIVER_SPRITE_PHASES 4u
#define PORTABLE_DRIVER_SPRITE_SLOTS 64u
#define PORTABLE_DRIVER_SPRITE_ROWS 16u
#define PORTABLE_DRIVER_SPRITE_TUMBLE 64u

enum
{
    /* Frames are filtered like the N64's three-point sampler; transparent
     * texels take their nearest opaque colour so edges keep no dark fringe. */
    PORTABLE_DRIVER_SPRITE_FILTERED = 1u
};

typedef struct
{
    uint32_t version, width, height, frame_count, phase_count, shared_colors;
    int32_t metres_per_pixel_q16, phase_distance_q16;
    uint32_t row_count, tumble_count, tumble_cycles, flags;
    int16_t row_pitch[PORTABLE_DRIVER_SPRITE_ROWS];
    uint16_t slots[PORTABLE_DRIVER_SPRITE_ROWS][PORTABLE_DRIVER_SPRITE_SLOTS];
    uint16_t tumble[PORTABLE_DRIVER_SPRITE_TUMBLE];
    /* Star: additive RGB cycled star_ticks each; zero colours keep the
     * engine's own star glow. Alphas: zero keeps the engine default. */
    uint32_t star_add[4];
    uint32_t star_ticks, ghost_alpha, boo_alpha;
    const uint8_t *shared;  /* shared_colors little-endian ARGB words */
    const uint8_t *frames;  /* frame_count frame records */
    size_t frame_bytes;
} PortableDriverSprites;

/* 1 with a valid section; the view borrows the section bytes. */
int portable_driver_sprites_open(const PortableAssetSectionView *section, PortableDriverSprites *out);
int portable_driver_sprites_find(const PortableAssetPackView *pack, uint32_t driver_id, PortableDriverSprites *out);
/* ARGB of a frame's texel in a phase. */
uint32_t portable_driver_sprites_texel(const PortableDriverSprites *sprites, uint32_t frame, uint32_t phase,
                                       uint32_t x, uint32_t y);
/* The pitch row for a nose-up pitch (binary angle, 65536 a turn). */
uint32_t portable_driver_sprites_row(const PortableDriverSprites *sprites, int32_t pitch);

#endif
