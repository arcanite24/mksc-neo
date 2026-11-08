#ifndef PORTABLE_MODERN_DRIFT_ASSET_H
#define PORTABLE_MODERN_DRIFT_ASSET_H

#include "asset_pack.h"
#include "modern_drift.h"

/* Decode an explicitly selected profile; absence never enables modern rules.
 * Failed decoding leaves the caller's rules unchanged. */
int portable_modern_drift_asset_open(const PortableAssetSectionView *section,
                                   PortableModernDriftRules *out);

#endif
