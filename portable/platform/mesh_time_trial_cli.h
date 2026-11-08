#ifndef PORTABLE_MESH_TIME_TRIAL_CLI_H
#define PORTABLE_MESH_TIME_TRIAL_CLI_H
#include "asset_pack.h"
#include "settings.h"
/* Returns zero only after the independent 3D replay finishes verification. */
int portable_mesh_time_trial_verify_cli(const PortableAssetPackView *base, const char *path,
                                        const PortableSettingsOverrides *overrides,
                                        int require_finished);
#endif
