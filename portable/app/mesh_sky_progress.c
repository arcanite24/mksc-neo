#include "mesh_sky_progress.h"
#include <stdio.h>
#include <string.h>
static const char count_key[] = "source:progress-stage-count";
static const char raw_prefix[] = "source:progress-raw:";
static int fail(char *error, size_t capacity, uint32_t course, const char *reason)
{
    if (error && capacity)
        snprintf(error, capacity, "3D course=%u source progress: %s", course, reason);
    return 0;
}
int portable_mesh_sky_progress_config_valid(const PortableMeshSkyProgressConfig *config)
{
    if (!config || config->enabled > 1u ||
        (config->enabled ? !config->stage_count || config->stage_count > 1024u
                         : config->stage_count != 0u))
        return 0;
    for (unsigned i = 0; i < 1024u; ++i)
    {
        if (i >= config->stage_count)
        {
            if (config->source_raw[i])
                return 0;
        }
        else if (config->source_raw[i] > 127u ||
                 (i ? config->source_raw[i] < config->source_raw[i - 1u]
                    : config->source_raw[i] != 0u))
            return 0;
    }
    return 1;
}
static int stage_index(const char *key, uint32_t *index)
{
    const char *digits = key + sizeof raw_prefix - 1u;
    if (!*digits || (*digits == '0' && digits[1]))
        return 0;
    uint32_t value = 0u;
    for (const char *p = digits; *p; ++p)
    {
        if (*p < '0' || *p > '9' || value > 1023u / 10u)
            return 0;
        value = value * 10u + (unsigned)(*p - '0');
        if (value >= 1024u)
            return 0;
    }
    *index = value;
    return 1;
}
int portable_mesh_sky_progress_open(const PortableCourse3DView *course,
                                    PortableMeshSkyProgressConfig *out, char *error,
                                    size_t capacity)
{
    PortableCourse3DMeta meta;
    const uint32_t id = course ? course->id : 0u;
    if (!course || !out || !portable_course_3d_meta(course, &meta) ||
        meta.property_count > 16384u || meta.property_first > UINT32_MAX - meta.property_count)
        return fail(error, capacity, id, "invalid META");
    PortableMeshSkyProgressConfig staged = {0};
    uint8_t seen[1024] = {0};
    unsigned entries = 0u;
    for (uint32_t i = 0; i < meta.property_count; ++i)
    {
        PortableCourse3DProperty property;
        if (!portable_course_3d_property_at(course, meta.property_first + i, &property))
            return fail(error, capacity, id, "invalid META property");
        if (!strcmp(property.key, count_key))
        {
            if (staged.enabled || property.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER ||
                property.value.integer < 1 || property.value.integer > 1024)
                return fail(error, capacity, id, "invalid/duplicate source:progress-stage-count");
            staged.enabled = 1;
            staged.stage_count = (uint32_t)property.value.integer;
        }
        else if (!strncmp(property.key, raw_prefix, sizeof raw_prefix - 1u))
        {
            uint32_t stage;
            if (!stage_index(property.key, &stage) || seen[stage] ||
                property.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER ||
                property.value.integer < 0 || property.value.integer > 127)
                return fail(error, capacity, id, "invalid/duplicate source:progress-raw:N");
            seen[stage] = 1;
            staged.source_raw[stage] = (uint8_t)property.value.integer;
            ++entries;
        }
    }
    if (entries != staged.stage_count || !portable_mesh_sky_progress_config_valid(&staged))
        return fail(error, capacity, id, "orphan, missing or unordered source:progress-raw:N");
    for (unsigned i = 0; i < staged.stage_count; ++i)
        if (!seen[i])
            return fail(error, capacity, id, "missing source:progress-raw:N");
    if (staged.enabled)
    {
        const PortableCourse3DTable *gates =
            portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('C', 'P', 'N', 'T'));
        if (!gates || !gates->count || gates->count > 1024u || gates->stride != 64u ||
            !gates->bytes)
            return fail(error, capacity, id, "missing/invalid CPNT stages");
        memset(seen, 0, sizeof seen);
        for (uint32_t i = 0; i < gates->count; ++i)
        {
            PortableCourse3DCheckpoint gate;
            if (!portable_course_3d_checkpoint(course, i, &gate) ||
                gate.stage >= staged.stage_count)
                return fail(error, capacity, id, "CPNT stage outside provenance");
            seen[gate.stage] = 1;
        }
        for (unsigned i = 0; i < staged.stage_count; ++i)
            if (!seen[i])
                return fail(error, capacity, id, "provenance stage absent from CPNT");
    }
    *out = staged;
    if (error && capacity)
        error[0] = 0;
    return 1;
}
int portable_mesh_sky_progress_valid(const PortableMeshSkyProgressConfig *config,
                                     const PortableMeshSkyProgress *world)
{
    if (!portable_mesh_sky_progress_config_valid(config) || !world)
        return 0;
    if (!config->enabled)
        return !world->high_water && !world->route_index && !world->route_wrap;
    if (world->route_wrap < 127u || world->route_index > 127u ||
        world->high_water >> 8u != world->route_wrap || (world->high_water & 255u) > 127u ||
        (world->high_water & 255u) < world->route_index ||
        (world->route_wrap == 127u && (world->route_index || (world->high_water & 255u))))
        return 0;
    unsigned current = 0u, high = 0u;
    for (unsigned i = 0; i < config->stage_count; ++i)
    {
        current |= config->source_raw[i] == world->route_index;
        high |= config->source_raw[i] == (world->high_water & 255u);
    }
    return current && high;
}
int portable_mesh_sky_progress_init(const PortableMeshSkyProgressConfig *config,
                                    PortableMeshSkyProgress *out)
{
    if (!out || !portable_mesh_sky_progress_config_valid(config))
        return 0;
    const PortableMeshSkyProgress next =
        config->enabled ? (PortableMeshSkyProgress){.high_water = 127u << 8u, .route_wrap = 127u}
                        : (PortableMeshSkyProgress){0};
    *out = next;
    return 1;
}
int portable_mesh_sky_progress_update(const PortableMeshSkyProgressConfig *config,
                                      PortableMeshSkyProgress *out,
                                      const PortableMeshSkyProgress *before,
                                      const PortableMeshSkyProgressInput *input)
{
    if (!out || !portable_mesh_sky_progress_valid(config, before))
        return 0;
    PortableMeshSkyProgress next = *before;
    if (config->enabled)
    {
        if (!input || input->stage >= config->stage_count || input->completed_laps > 127u ||
            input->crossing_armed > 1u || (input->stage && !input->crossing_armed))
            return 0;
        next.route_index = config->source_raw[input->stage];
        const uint32_t wrap =
            input->completed_laps || input->crossing_armed || before->route_wrap >= 128u
                ? 128u + input->completed_laps
                : 127u;
        if (wrap < before->route_wrap)
            return 0;
        next.route_wrap = (uint8_t)wrap;
        const uint16_t progress = (uint16_t)((wrap << 8u) | next.route_index);
        if (progress > next.high_water)
            next.high_water = progress;
    }
    if (!portable_mesh_sky_progress_valid(config, &next))
        return 0;
    *out = next;
    return 1;
}
