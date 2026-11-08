#ifndef PORTABLE_MESH_ITEMS_STATE_H
#define PORTABLE_MESH_ITEMS_STATE_H
#include "mesh_items.h"
/* Versioned, explicit LE-word domain for replay verification. No structure
 * padding, pointers, artwork, borrowed rules or host byte order enter this hash.
 * Hashing reads state only; validity/admission belongs to the world owner. */
uint64_t portable_mesh_items_checksum(const PortableMeshItems *world);
#endif
