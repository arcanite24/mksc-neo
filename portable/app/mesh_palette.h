#ifndef PORTABLE_MESH_PALETTE_H
#define PORTABLE_MESH_PALETTE_H
#include "ambient.h"

enum
{
    PORTABLE_MESH_PALETTE_RULES_KIND = 107,
    PORTABLE_MESH_PALETTE_HEADER_BYTES = 48,
    PORTABLE_MESH_PALETTE_BINDINGS = 128,
    PORTABLE_MESH_PALETTE_VARIANTS = 512,
    PORTABLE_MESH_PALETTE_PAIR_BYTES = 256,
    PORTABLE_MESH_PALETTE_INDEX_BYTES = 4 * 1024 * 1024
};
typedef struct
{
    uint32_t kind, id, bank, width, height, index_offset, index_size;
} PortableMeshPaletteBinding;
/* Plain copies are valid: only immutable caller-owned wire data is borrowed.
 * AmbientRules views are rebuilt on each call, never stored with self-pointers.
 * Variants contain road64 then sky64 little-endian RGB555 words. */
typedef struct
{
    uint32_t enabled, profile_id, revision, binding_count, variant_count;
    uint32_t index_bytes, data_size, object_seed_offset;
    uint16_t lane_count, frame_count, patch_count, init_patch_count;
    uint16_t sunset_lane, timer_lane; /* UINT16_MAX means no timer. */
    const uint8_t *data, *indices;
    PortableMeshPaletteBinding bindings[PORTABLE_MESH_PALETTE_BINDINGS];
    PortableAmbientLane lanes[2];
    PortableAmbientFrame frames[PORTABLE_AMBIENT_FRAMES];
    PortableAmbientPatch patches[PORTABLE_AMBIENT_PATCHES], initial[32];
    PortableAmbientSeed seed;
    PortableAmbientSnapshot initial_snapshot;
    uint8_t variants[PORTABLE_MESH_PALETTE_VARIANTS][PORTABLE_MESH_PALETTE_PAIR_BYTES];
} PortableMeshPaletteConfig;
typedef struct
{
    uint32_t enabled, profile_id, revision, variant;
    PortableAmbientState ambient;
} PortableMeshPalette;

/* Decode deeply checks all planes and enumerates immutable variants once.
 * Failure preserves output. Wire bytes must live as long as any config copy. */
int portable_mesh_palette_decode(const uint8_t *, size_t, uint32_t content_id,
                                 PortableMeshPaletteConfig *, char *, size_t);
/* Fast owner-side validation excludes rescanning index planes/variant tables. */
int portable_mesh_palette_config_valid(const PortableMeshPaletteConfig *);
int portable_mesh_palette_valid(const PortableMeshPaletteConfig *, const PortableMeshPalette *);
/* Internal composition only: caller must have successfully validated this exact
 * immutable config in the current call. No cached validation across calls.
 * These retain all owner/input checks and transactional output semantics. */
int portable_mesh_palette_state_valid(const PortableMeshPaletteConfig *,
                                      const PortableMeshPalette *);
int portable_mesh_palette_tick_validated_config(const PortableMeshPaletteConfig *,
                                                PortableMeshPalette *, const PortableMeshPalette *,
                                                const PortableAmbientInputs *, const int32_t *sine,
                                                size_t sine_count, char *, size_t);
int portable_mesh_palette_init(const PortableMeshPaletteConfig *, PortableMeshPalette *, char *,
                               size_t);
/* One accepted turn: PRE then POST with the caller's before-sky state and
 * after-sky phase. Rendering never calls this. Aliasing is supported; all
 * failures preserve out. paused freezes the complete owner. */
int portable_mesh_palette_tick(const PortableMeshPaletteConfig *, PortableMeshPalette *,
                               const PortableMeshPalette *, const PortableAmbientInputs *,
                               const int32_t *sine, size_t sine_count, char *, size_t);
const uint8_t *portable_mesh_palette_variant(const PortableMeshPaletteConfig *, unsigned variant);
int portable_mesh_palette_binding(const PortableMeshPaletteConfig *, unsigned index,
                                  PortableMeshPaletteBinding *, const uint8_t **indices);
/* Canonical named fields, no pointers or padding; absent owner preserves h. */
uint64_t portable_mesh_palette_hash(const PortableMeshPalette *, uint64_t h);
uint64_t portable_mesh_palette_checksum(const PortableMeshPaletteConfig *,
                                        const PortableMeshPalette *);
#endif
