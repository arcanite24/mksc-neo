#include "mesh_time_trial_cli.h"
#include "mesh_time_trial_replay.h"
#include "mesh_kart_rules_asset.h"
#include "mesh_item_rules_asset.h"
#include "mesh_collision_index.h"
#include <stdlib.h>
#include "mod_loader.h"
#include <stdio.h>
#include <string.h>

static int file_read(void *context, uint8_t *byte)
{
    FILE *file = context;
    const int value = fgetc(file);
    if (value == EOF)
        return ferror(file) ? -1 : 0;
    *byte = (uint8_t)value;
    return 1;
}

static size_t file_write(void *context, const char *bytes, size_t size)
{
    return fwrite(bytes, 1u, size, context);
}

int portable_mesh_time_trial_verify_cli(const PortableAssetPackView *base, const char *path,
                                        const PortableSettingsOverrides *overrides,
                                        int require_finished)
{
    if (base == NULL || path == NULL || overrides == NULL)
        return 2;
    FILE *input = fopen(path, "rb");
    if (input == NULL)
    {
        fprintf(stderr, "Could not read 3D Time Trial replay: %s\n", path);
        return 1;
    }
    long size;
    if (fseek(input, 0, SEEK_END) != 0 || (size = ftell(input)) <= 0 ||
        (uint64_t)size > PORTABLE_MESH_REPLAY_ACTORS_MAX_BYTES || fseek(input, 0, SEEK_SET) != 0)
    {
        fputs("3D Time Trial replay exceeds the bounded stream size\n", stderr);
        fclose(input);
        return 1;
    }
    PortableReplayReader reader = {input, file_read, 0};
    PortableMeshReplayHeader header;
    if (portable_mesh_replay_inspect_header(&reader, &header) != PORTABLE_MESH_REPLAY_OK)
    {
        fputs("Invalid 3D Time Trial replay header\n", stderr);
        fclose(input);
        return 1;
    }
    PortableSettings settings;
    portable_settings_defaults(&settings);
    portable_settings_apply(&settings, overrides);
    if (settings.mod_count != 0u && !overrides->no_mods && overrides->mods_dir == NULL)
    {
        fputs("3D replay mods require --mods-dir\n", stderr);
        fclose(input);
        return 2;
    }
    int result = 1;
    const PortableAssetPackView *selected = base;
    PortableModSet *mods = NULL;
    PortableMeshCollisionIndex *collision_index = NULL;
    if (settings.mod_count != 0u && !overrides->no_mods)
    {
        PortableModLoadReport report;
        mods = portable_mods_load_domains_strict(
            base, base, overrides->mods_dir,
            (const char (*)[PORTABLE_MOD_ID_CAPACITY])settings.mods, settings.mod_count, &report);
        if (mods == NULL)
        {
            fprintf(stderr, "3D replay mod candidate: %s\n", report.first_error);
            fclose(input);
            return 1;
        }
        char source_error[PORTABLE_MOD_ERROR_CAPACITY];
        if(!portable_mods_select_assets(mods,base,base,
            (const char (*)[PORTABLE_MOD_ID_CAPACITY])settings.asset_sources,source_error,sizeof source_error)){
            fprintf(stderr,"3D replay asset choices: %s\n",source_error);
            portable_mods_free(mods);fclose(input);return 1;
        }
        selected = portable_mods_pack_3d(mods);
    }
    if (memcmp(selected->deterministic_digest, header.compatibility.digest, 32u) != 0)
        fputs("3D replay content digest does not match the selected courses\n", stderr);
    else
    {
        PortableCourse3DView course = {0};
        PortableCourse3DMeta meta = {0};
        PortableMeshKartRules rules = {0};
        PortableMeshTimeTrialSession session = {0};
        char error[256] = {0};
        int prepared =
            portable_course_3d_open(selected, header.compatibility.course_id, &course, error,
                                    sizeof error) &&
            portable_course_3d_meta(&course, &meta) &&
            portable_mesh_kart_rules_asset_load(selected, meta.physics_profile_id, &rules);
        PortableCourse3DChase chase;
        if (prepared)
            prepared = portable_course_3d_chase(&course, &chase, error, sizeof error) &&
                       portable_mesh_kart_rules_scale_body(&rules, chase.kart_scale_q16);
        if (prepared)
        {
            PortableCourse3DProperty declaration;
            if ((portable_course_3d_property_find(&course, meta.property_first, meta.property_count,
                                                  "mksc:actor-rules", &declaration) ||
                 portable_course_3d_property_find(&course, meta.property_first, meta.property_count,
                                                  "mksc:coin-rules", &declaration) ||
                 portable_course_3d_property_find(&course, meta.property_first, meta.property_count,
                                                  "mksc:sky-motion", &declaration)) &&
                header.version != PORTABLE_MESH_REPLAY_ACTORS_VERSION)
            {
                prepared = 0;
                snprintf(error, sizeof error, "course=%u requires an actor-aware v3 replay",
                         course.id);
            }
        }
        if (prepared)
            prepared = portable_mesh_time_trial_init(&session, &course, &rules, 0u, error,
                                                     sizeof error) == PORTABLE_MESH_OK;
        if (prepared)
        {
            collision_index = portable_mesh_collision_index_create(&course, error, sizeof error);
            prepared = collision_index != NULL;
            session.geometry = portable_mesh_collision_index_course(collision_index);
        }
        if (!prepared)
            fprintf(stderr, "3D Time Trial replay content: %s\n",
                    error[0] ? error : "invalid physics profile or course");
        else
        {
            PortableMeshTimeTrialCompatibility expected = header.compatibility;
            expected.course_id = course.id;
            expected.profile_id = rules.profile_id;
            expected.profile_revision = rules.revision;
            expected.tick_numerator = 4389u;
            expected.tick_denominator = 262144u;
            memcpy(expected.digest, selected->deterministic_digest, sizeof expected.digest);
            rewind(input);
            reader = (PortableReplayReader){input, file_read, 0};
            PortableReplayWriter diagnostics = {stderr, file_write, 0};
            size_t ticks = 0u;
            PortableMeshReplayStatus status;
            if (header.version == PORTABLE_MESH_REPLAY_ITEMS_VERSION ||
                header.version == PORTABLE_MESH_REPLAY_ACTORS_VERSION)
            {
                const int with_items = header.item_profile_id != 0u;
                PortableMeshItems *items = with_items ? calloc(1, sizeof *items) : NULL;
                const int with_actors = header.version == PORTABLE_MESH_REPLAY_ACTORS_VERSION;
                PortableMeshCourseActorsConfig *actor_config =
                    with_actors ? calloc(1, sizeof *actor_config) : NULL;
                PortableMeshCourseActors *actors = with_actors ? calloc(1, sizeof *actors) : NULL;
                PortableMeshItemRules item_rules;
                uint32_t item_id = 0;
                PortableMeshItemsConfig config = {.course = &course,
                                                  .rules = &item_rules,
                                                  .kart_rules = &rules,
                                                  .mode = 2u,
                                                  .count = 1u,
                                                  .human_mask = 1u,
                                                  .geometry = session.geometry};
                if ((with_items && !items) || (with_actors && (!actors || !actor_config)))
                    status = PORTABLE_MESH_REPLAY_IO_ERROR;
                else if (!with_actors && (uint64_t)size > PORTABLE_MESH_REPLAY_ITEMS_MAX_BYTES)
                    status = PORTABLE_MESH_REPLAY_LIMIT;
                else if (with_items &&
                         (!portable_course_3d_item_rules(&course, &item_id, error, sizeof error) ||
                          !item_id ||
                          !portable_mesh_item_rules_asset_load(selected, item_id, &item_rules) ||
                          portable_mesh_items_init(items, &config, header.item_seed, error,
                                                   sizeof error) != PORTABLE_MESH_ITEMS_OK))
                    status = PORTABLE_MESH_REPLAY_INCOMPATIBLE;
                else
                {
                    PortableMeshReplayPlayback playback;
                    if (with_actors && (!portable_mesh_course_actors_config_open(
                                            &course, actor_config, error, sizeof error) ||
                                        !portable_mesh_course_actors_init_on_course(actor_config, session.geometry, actors,
                                                                          error, sizeof error)))
                        status = PORTABLE_MESH_REPLAY_INCOMPATIBLE;
                    else if (!with_items && (!with_actors || !actor_config->sky.enabled ||
                                             portable_mesh_course_actors_needs_items(actor_config)))
                    {
                        snprintf(error, sizeof error,
                                 "course=%u: itemless v3 requires a sky-only owner", course.id);
                        fprintf(stderr, "3D Time Trial replay content: %s\n", error);
                        status = PORTABLE_MESH_REPLAY_INCOMPATIBLE;
                    }
                    else
                        status = with_actors
                                     ? portable_mesh_replay_playback_init_actors(
                                           &playback, &reader, &expected, &session, items,
                                           with_items ? &config : NULL, actors, actor_config)
                                     : portable_mesh_replay_playback_init_items(
                                           &playback, &reader, &expected, &session, items, &config);
                    while (status == PORTABLE_MESH_REPLAY_OK)
                    {
                        status = portable_mesh_replay_playback_tick(&playback, &diagnostics);
                        ticks = playback.ticks;
                    }
                }
                free(items);
                free(actors);
                free(actor_config);
            }
            else if ((uint64_t)size > PORTABLE_MESH_REPLAY_MAX_BYTES)
                status = PORTABLE_MESH_REPLAY_LIMIT;
            else
                status =
                    portable_mesh_replay_run(&reader, &expected, &session, &diagnostics, &ticks);
            if (status == PORTABLE_MESH_REPLAY_COMPLETE &&
                (!require_finished || (session.state.phase == PORTABLE_MESH_TT_FINISHED &&
                                       session.state.lap >= meta.laps)))
            {
                printf("3D Time Trial replay: PASS course=%u driver=%u ticks=%zu phase=%u "
                       "laps=%u/%u race_ticks=%u checksum=%016llx\n",
                       course.id, expected.driver_id, ticks, session.state.phase, session.state.lap,
                       meta.laps, session.state.race_ticks,
                       (unsigned long long)portable_mesh_time_trial_checksum(&session.state));
                result = 0;
            }
            else
                fprintf(stderr, "3D Time Trial replay: %s%s\n",
                        portable_mesh_replay_status_name(status),
                        require_finished ? " (a finished race is required)" : "");
        }
    }
    portable_mesh_collision_index_close(collision_index);
    portable_mods_free(mods);
    if (result != 0)
        fputs("3D replay verification failed; enable its exact content and physics profile\n",
              stderr);
    fclose(input);
    return result;
}
