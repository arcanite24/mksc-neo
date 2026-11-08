#include "course_prop_contact.h"
#include "gp_race_roster.h"
#include "gp_roster_math.h"

#include "driver_vector_effect.h"
#include "kart_motion.h"
#include "driver_hop.h"
#include "collision.h"
#include "driver_palette.h"

#include <stddef.h>
#include <string.h>

static int16_t signed16(uint16_t value);

static uint8_t initial_rank_row(PortableGpRaceRoster *roster, PortableGpRacer *racer,
                                size_t checkpoint_count);
static PortableGpRosterStatus update_standings_for_racer(PortableGpRaceRoster *roster,
                                                         size_t racer_index, uint8_t compare_laps);

PortableGpRosterStatus portable_gp_race_roster_decide_initial(PortableGpRaceRoster *roster,
                                                              const PortableRaceRulesView *rules,
                                                              const PortableTrackRaster *raster)
{
    if (roster == NULL || rules == NULL || raster == NULL || rules->route == NULL ||
        rules->route_count == 0u || rules->cpu_tuning == NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (roster->course_id != rules->compatibility.course_id)
        return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;

    for (size_t index = 1u; index < portable_gp_race_roster_count(roster); ++index)
    {
        PortableGpRacer *racer = &roster->racers[index];
        const int32_t x = fixed_to_int(racer->position_x_q16);
        const int32_t y = fixed_to_int(racer->position_y_q16);
        uint8_t raw_route_index;
        uint8_t route_index;
        int32_t checkpoint_x;
        int32_t checkpoint_y;
        int32_t delta_x;
        int32_t delta_y;
        uint16_t raw_angle = 0u;

        if (portable_track_raster_query16(raster, x, y, &raw_route_index) !=
            PORTABLE_TRACK_RASTER_OK)
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
        route_index = (uint8_t)(raw_route_index & UINT8_C(0x7f));
        if ((size_t)route_index >= rules->route_count)
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;

        /* The portable route stores source checkpoints in world units. The
         * source table queried by sub_8032AD0/sub_8032B10 stores world/8 and
         * sub_803285C deliberately narrows each coordinate to one byte. */
        checkpoint_x = rules->route[route_index].x / 8;
        checkpoint_y = rules->route[route_index].y / 8;
        delta_x = ((int32_t)(uint8_t)checkpoint_x - x / 8) / 2;
        delta_y = ((int32_t)(uint8_t)checkpoint_y - y / 8) / 2;
        if (portable_driver_vector_effect_source_angle(
                (int16_t)delta_x, (int16_t)delta_y, &raw_angle) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;

        racer->route_index = route_index;
        racer->control = UINT16_C(0x8000);
        racer->route_angle = (uint16_t)(raw_angle & UINT16_C(0xff00));
        racer->acceleration_q16 =
            (int32_t)rules->cpu_tuning->acceleration[racer->driver_class][0] * INT32_C(4096);
        racer->decision_valid = 1u;
    }
    /* The native decision boundary corresponds to source wrapper 1's return:
     * all decisions are installed and selector 3 has produced the row that
     * wrapper 2 consumes for its first motion tick. */
    if (roster->rank_cursor != 0u && (roster->race_mode_flags_7da & UINT16_C(0x0800)) == 0u)
    {
        PortableGpRacer *ranked = &roster->racers[roster->rank_cursor];
        ranked->rank_row_9a = initial_rank_row(roster, ranked, rules->route_count);
    }
    roster->rank_cursor =
        (uint8_t)((roster->rank_cursor + 1u) % portable_gp_race_roster_count(roster));
    return PORTABLE_GP_ROSTER_OK;
}

void portable_gp_race_roster_apply_route_wrap(PortableGpRacer *racer,
                                              const PortableRaceRulesView *rules, uint8_t new_wrap)
{
    const uint8_t previous_wrap = racer->route_wrap_91;
    const uint16_t wrapped_progress = (uint16_t)((uint16_t)new_wrap << 8u);
    const uint32_t finish_progress = (UINT32_C(0x80) + rules->lap_count) << 8u;

    /* sub_803B7B8 leaves finished rivals at wrap 0x83 even when their
     * physical kart circles the course again. The retained Peach trace
     * holds +0x91 at 131 for all seven rivals through source frame 8719. */
    if ((racer->source_flags & UINT32_C(0x20)) != 0u &&
        ((uint32_t)previous_wrap << 8u) >= finish_progress)
        return;
    racer->route_wrap_91 = new_wrap;
    if (new_wrap <= previous_wrap || wrapped_progress <= racer->max_progress_c6)
        return;

    /* sub_803B7B8 writes +0xc6 only for a new high-water wrap.  The first
     * 0x7f -> 0x80 transition arms the race without a lap cue. */
    racer->max_progress_c6 = wrapped_progress;
    if (new_wrap == UINT8_C(0x80) || finish_progress > UINT16_MAX)
        return;
    if (wrapped_progress >= finish_progress)
    {
        /* sub_803B7B8 marks a racer finished when +0xc6 reaches the GP
         * finish threshold (+0x7e6). The retained Peach source route sets
         * +0x48 from 0x200c to 0x203c at rival 6's frame-5729 wrap and
         * rival 7's frame-5736 wrap. Standings and row selection read 0x20. */
        racer->source_flags = (racer->source_flags & ~UINT32_C(0x38)) | UINT32_C(0x2038);
        racer->hit_spin_82 = 0;
        racer->hit_angle_80 = 0u;
        racer->hit_state_7e = 0u;
        racer->mushroom_mode_84 = 0u;
        return;
    }
    if ((uint32_t)wrapped_progress + UINT32_C(0x100) == finish_progress)
    {
        racer->source_flags |= UINT32_C(0x0008);
        /* sub_803B7B8 marks the penultimate wrap for the lap cue.
         * Direct original frame 6186: +0xa4=0x0080, +0x48=0x800c. */
        racer->lap_marker_a4 |= UINT16_C(0x0080);
    }
    else
    {
        racer->source_flags |= UINT32_C(0x0004);
        if ((racer->source_flags & UINT32_C(0x8000)) != 0u)
            racer->lap_marker_a4 |= UINT16_C(0x0100);
    }
}

PortableGpRosterStatus portable_gp_race_roster_track_progress(PortableGpRacer *racer,
                                                              const PortableRaceRulesView *rules,
                                                              const PortableTrackRaster *raster,
                                                              uint8_t ai_mode_row)
{
    uint8_t cell;
    if (racer == NULL || rules == NULL || raster == NULL || rules->route == NULL ||
        rules->route_count == 0u || rules->cpu_tuning == NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer->source_flags &= ~UINT32_C(3);
    racer->acceleration_mode_19b = 0u;
    const int status = portable_track_raster_query16(raster, fixed_to_int(racer->position_x_q16),
                                                     fixed_to_int(racer->position_y_q16), &cell);
    if (status == PORTABLE_TRACK_RASTER_OUT_OF_BOUNDS)
    {
        racer->source_flags |= UINT32_C(1);
        return PORTABLE_GP_ROSTER_OK;
    }
    if (status != PORTABLE_TRACK_RASTER_OK)
        return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
    const uint8_t checkpoint = cell & UINT8_C(0x7f);
    if (checkpoint != UINT8_C(0x7f) && checkpoint >= rules->route_count)
        return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
    if (checkpoint == UINT8_C(0x7f))
        racer->source_flags |= UINT32_C(2);
    if ((cell & UINT8_C(0x80)) == 0u)
    {
        if (checkpoint == UINT8_C(0x7f))
            return PORTABLE_GP_ROSTER_OK;
        if (rules->ai_modes != NULL)
        {
            const size_t slot = (size_t)ai_mode_row * rules->route_count + checkpoint;
            if (slot >= rules->ai_mode_count)
                return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
            racer->acceleration_mode_19b = rules->ai_modes[slot];
        }
        /* BA52 compares height with0x80<<11: four Q16 units, not sixteen.
         * Only low-half checkpoints with mode bit7 use this shortcut gate. */
        if ((racer->acceleration_mode_19b & UINT8_C(0x80)) != 0u &&
            racer->world_height_q16 > INT32_C(0x40000))
            racer->source_flags |= UINT32_C(2);
        else
            racer->route_index = checkpoint;
        return PORTABLE_GP_ROSTER_OK;
    }
    if (checkpoint != UINT8_C(0x7f))
        racer->route_index = checkpoint;
    /* B98C calls B7B8 on every high-half cell, including FF. Carry mode6
     * and finished racers skip it; finite-map misses never call it. */
    if (rules->finish_kind == PORTABLE_FINISH_NONE || racer->lightning_state_78 == 6u ||
        (racer->source_flags & UINT32_C(0x20)) != 0u)
        return PORTABLE_GP_ROSTER_OK;
    racer->source_flags &= ~UINT32_C(0xc0);
    const int16_t old_y = racer->contact_center_y;
    const int16_t new_y = signed16((uint16_t)((uint32_t)racer->position_y_q16 >> 16u));
    const int16_t finish_y = signed16((uint16_t)((uint32_t)rules->finish_y_q16 >> 16u));
    if (old_y < finish_y && new_y >= finish_y)
    {
        if (racer->route_wrap_91 > UINT8_C(0x74))
            --racer->route_wrap_91;
        return PORTABLE_GP_ROSTER_OK;
    }
    if (old_y < finish_y || new_y >= finish_y)
        return PORTABLE_GP_ROSTER_OK;
    const uint16_t previous_max = racer->max_progress_c6;
    portable_gp_race_roster_apply_route_wrap(racer, rules, (uint8_t)(racer->route_wrap_91 + 1u));
    racer->route_index = 0u;
    if (racer->max_progress_c6 > previous_max && racer->route_wrap_91 != UINT8_C(0x80) &&
        (racer->source_flags & UINT32_C(0x2000)) != 0u && rules->gp_target_table != NULL)
    {
        /* B95E/DCA4 changes rows only on a new high-water lap. */
        const size_t slot =
            ((size_t)racer->rank_flags_ba >> 1u) * 5u + ((size_t)racer->route_wrap_91 & 0x7fu);
        if ((racer->source_flags & UINT32_C(0x20)) != 0u)
            racer->route_row_191 = (uint8_t)((racer->source_flags & UINT32_C(0x8000)) != 0u);
        else if (slot < rules->gp_target_table_count)
            racer->route_row_191 = rules->gp_target_table[slot];
        else
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
    }
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpRosterStatus update_route_angle(PortableGpRacer *racer,
                                                 const PortableRaceRulesView *rules,
                                                 const PortableTrackRaster *raster,
                                                 const uint16_t *angle_lut, size_t angle_lut_count)
{
    const int32_t x = fixed_to_int(racer->position_x_q16);
    const int32_t y = fixed_to_int(racer->position_y_q16);
    if (racer->battle_navigation_valid)
    {
        uint16_t angle;
        if (portable_driver_vector_effect_source_angle((int16_t)(racer->battle_waypoint_x - x),
                                                       (int16_t)(racer->battle_waypoint_y - y),
                                                       &angle) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
        racer->route_angle = (uint16_t)(angle & 0xff00u);
        return PORTABLE_GP_ROSTER_OK;
    }
    uint8_t route_index = racer->route_index;
    int32_t checkpoint_x;
    int32_t checkpoint_y;
    int32_t delta_x;
    int32_t delta_y;
    uint16_t raw_angle = 0u;
    const PortableRacePoint *target;
    const int outside = (racer->source_flags & UINT32_C(3)) != 0u;
    if (!outside)
    {
        /* BC84/3285C samples the current cell independently of saved90.
         * B7B8 can reset90 at a finish crossing on a different checkpoint. */
        uint8_t cell;
        if (portable_track_raster_query16(raster, x, y, &cell) != PORTABLE_TRACK_RASTER_OK)
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
        route_index = cell & UINT8_C(0x7f);
    }
    if (!outside && route_index == UINT8_C(0x7f))
    {
        /* A800's X leg does not resample B98C.3285C/32AD0 can read127
         * with clear outside flags; use the imported source target for it. */
        if (racer->route_row_191 >= 3u || rules->gp_sentinel_targets == NULL ||
            !(rules->gp_sentinel_target_mask & (1u << racer->route_row_191)))
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
        target = &rules->gp_sentinel_targets[racer->route_row_191];
    }
    else
    {
        if (route_index >= rules->route_count)
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
        target = &rules->route[route_index];
        if (!outside && racer->route_row_191 != 0u)
        {
            const size_t row = (size_t)racer->route_row_191 - 1u;
            if (rules->gp_route_rows == NULL || row >= rules->gp_route_row_count)
                return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
            target = &rules->gp_route_rows[row * rules->route_count + route_index];
        }
    }
    if ((racer->source_flags & UINT32_C(3)) != 0u)
    {
        /* BC84 bypasses the alternate CPU row outside the route plane and
         * calls EC38 on full world-coordinate deltas, not EAEC's byte grid. */
        delta_x = rules->route[route_index].x - x;
        delta_y = rules->route[route_index].y - y;
    }
    else if (angle_lut != NULL)
    {
        /* 3285C calls EAEC's integer lookup on the byte-grid delta.
         * EC38's arctangent is reserved for BC84's outside-route branch. */
        if (portable_driver_track_direction_source_angle(
                racer->position_x_q16, racer->position_y_q16, target->x, target->y, angle_lut,
                angle_lut_count, &raw_angle) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
        delta_x = 0;
        delta_y = 0;
    }
    else
    {
        checkpoint_x = target->x / 8;
        checkpoint_y = target->y / 8;
        delta_x = ((int32_t)(uint8_t)checkpoint_x - x / 8) / 2;
        delta_y = ((int32_t)(uint8_t)checkpoint_y - y / 8) / 2;
    }
    if (((racer->source_flags & UINT32_C(3)) != 0u || angle_lut == NULL) &&
        portable_driver_vector_effect_source_angle((int16_t)delta_x, (int16_t)delta_y,
                                                   &raw_angle) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
        return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
    racer->route_angle = (uint16_t)(raw_angle & UINT16_C(0xff00));
    return PORTABLE_GP_ROSTER_OK;
}

static uint16_t approach_initial_heading(uint16_t current, uint16_t target, uint8_t rank_row,
                                         const PortableGpCpuTuning *tuning)
{
    int32_t difference = (int16_t)(uint16_t)(target - current);
    uint32_t magnitude;
    uint32_t table_index;
    uint32_t step;

    if (difference >= -0x200 && difference <= 0x1ff)
        return target;
    magnitude = (uint32_t)(difference < 0 ? -difference : difference);
    if (magnitude > 0x1ffu)
        magnitude = 0x1ffu;
    table_index = (magnitude >> 6) & 7u;
    /* BB48 masks the low nibble; all sixteen imported rows are addressable. */
    const uint8_t row = rank_row & 0x0fu;
    step = (uint32_t)tuning->heading_steps[row][table_index] << 4;
    return (uint16_t)(difference < 0 ? current - step : current + step);
}

static int32_t add_component(int32_t position, int16_t component)
{
    return (int32_t)((uint32_t)position + (uint32_t)((int32_t)component * INT32_C(0x100)));
}

static int16_t signed16(uint16_t value)
{
    return value < UINT16_C(0x8000) ? (int16_t)value
                                    : (int16_t)(-((int32_t)UINT16_C(0x10000) - value));
}

static int32_t signed32_bits(uint32_t value)
{
    return value < UINT32_C(0x80000000) ? (int32_t)value
                                        : (int32_t)(-((int64_t)UINT64_C(0x100000000) - value));
}

/* E68C/E5A4 sign-extend the angle and truncate division by 64 toward zero.
 * Keep the magnitude unsigned and bounded so the final 16-bit angle wrap
 * precedes division; optimized MSVC can otherwise fold away that wrap. */
static uint32_t impulse_sine_index(uint16_t angle)
{
    const uint32_t bits = angle;
    const uint32_t negative = bits & UINT32_C(0x8000);
    const uint32_t magnitude = negative ? UINT32_C(0x10000) - bits : bits;
    const uint32_t quotient = magnitude / UINT32_C(64);
    return (negative ? UINT32_C(0) - quotient : quotient) & UINT32_C(0x3ff);
}

static int32_t rounded_pair(int32_t first_table, int16_t first_value, int32_t second_table,
                            int16_t second_value)
{
    uint32_t bits = (uint32_t)((int64_t)first_table * first_value);
    int32_t product;
    bits += (uint32_t)((int64_t)second_table * second_value);
    product = (int32_t)bits;
    if (product < 0)
        product = (int32_t)((uint32_t)product + UINT32_C(0x7fff));
    return product >= 0 ? product / INT32_C(0x8000)
                        : -(int32_t)((-(int64_t)product + 0x7fff) / 0x8000);
}

static int16_t clamp_contact_component(int32_t value)
{
    if (value > 1024)
        value = 1024;
    if (value < -1024)
        value = -1024;
    return (int16_t)value;
}

static void note_player_bump(PortableGpRaceRoster *roster, size_t first, size_t second)
{
    const int32_t a = roster->racers[first].speed_q16, b = roster->racers[second].speed_q16;
    if (first != roster->local_racer && second != roster->local_racer)
        return;
    roster->player_bump_pending = 1u;
    roster->player_bump_speed_q16 = a > b ? a : b;
}

static PortableGpRosterStatus apply_kart_contact_response(PortableGpRaceRoster *roster,
                                                          size_t first_index, size_t second_index,
                                                          int16_t center_delta_x,
                                                          int16_t center_delta_y,
                                                          const int32_t *sine, size_t sine_count)
{
    PortableGpRacer *first = &roster->racers[first_index];
    PortableGpRacer *second = &roster->racers[second_index];
    uint32_t first_weight;
    uint32_t second_weight;
    uint16_t impact_angle;
    int16_t signed_angle;
    int32_t angle_index;
    int32_t axis_sine;
    int32_t axis_cosine;
    int32_t first_normal;
    int32_t first_tangent;
    int32_t second_normal;
    int32_t second_tangent;
    int32_t next_first_normal;
    int32_t next_second_normal;
    uint32_t weight_sum;

    if (sine == NULL || sine_count < 1024u || first_index == second_index ||
        first_index >= portable_gp_race_roster_count(roster) ||
        second_index >= portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (portable_gp_race_roster_contact_weight(roster, first_index, &first_weight) !=
            PORTABLE_GP_ROSTER_OK ||
        portable_gp_race_roster_contact_weight(roster, second_index, &second_weight) !=
            PORTABLE_GP_ROSTER_OK)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    note_player_bump(roster, first_index, second_index);
    if (first_weight < second_weight)
    {
        PortableGpRacer *swap_racer = first;
        uint32_t swap_weight = first_weight;
        first = second;
        second = swap_racer;
        first_weight = second_weight;
        second_weight = swap_weight;
        center_delta_x = (int16_t)-center_delta_x;
        center_delta_y = (int16_t)-center_delta_y;
    }
    if (portable_driver_vector_effect_source_angle(center_delta_x, center_delta_y, &impact_angle) !=
        PORTABLE_DRIVER_VECTOR_EFFECT_OK)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;

    /* Normal-size sub_803E92C selects one of four collision axes relative
     * to the heavier kart's heading before resolving a one-dimensional
     * elastic collision along that axis. */
    {
        const uint16_t heading = first->heading;
        const uint16_t opposite = (uint16_t)(heading - UINT16_C(0x8000));
        const int16_t difference = signed16((uint16_t)(opposite - impact_angle));
        if (difference >= 0)
        {
            if (difference <= INT16_C(0x2000))
                impact_angle = (uint16_t)(heading + UINT16_C(0x6000));
            if (signed16((uint16_t)(opposite - impact_angle)) > INT16_C(0x6000))
                impact_angle = (uint16_t)(heading + UINT16_C(0x2000));
        }
        else
        {
            if (difference >= -INT16_C(0x2000))
                impact_angle = (uint16_t)(heading + UINT16_C(0xa000));
            if (signed16((uint16_t)(opposite - impact_angle)) <= -INT16_C(0x6000))
                impact_angle = (uint16_t)(heading + UINT16_C(0xe000));
        }
    }
    signed_angle = signed16(impact_angle);
    angle_index =
        signed_angle < 0 ? -((-(int32_t)(signed_angle + 0x3f) + 0x3f) / 64) : signed_angle / 64;
    axis_sine = sine[(uint32_t)angle_index & UINT32_C(0x3ff)];
    axis_cosine = sine[(uint32_t)(0x100 - angle_index) & UINT32_C(0x3ff)];
    first_normal = rounded_pair(axis_sine, first->component_x, -axis_cosine, first->component_y);
    first_tangent = rounded_pair(axis_cosine, first->component_x, axis_sine, first->component_y);
    second_normal = rounded_pair(axis_sine, second->component_x, -axis_cosine, second->component_y);
    second_tangent = rounded_pair(axis_cosine, second->component_x, axis_sine, second->component_y);
    weight_sum = first_weight + second_weight;
    if (weight_sum == 0u || weight_sum > INT32_MAX)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    next_first_normal = ((int32_t)(first_weight - second_weight) * first_normal +
                         (int32_t)(2u * second_weight) * second_normal) /
                        (int32_t)weight_sum;
    next_second_normal = ((int32_t)(2u * first_weight) * first_normal +
                          (int32_t)(second_weight - first_weight) * second_normal) /
                         (int32_t)weight_sum;
    if (next_first_normal - next_second_normal <= 0x200)
    {
        if (first->hit_cue_flags_58 != second->hit_cue_flags_58)
            next_second_normal = next_first_normal - 0x200;
        else
        {
            /* F32C rounds signed halves toward zero. The second midpoint
             * deliberately reads the first normal AFTER its +256 update. */
            next_first_normal =
                next_second_normal + (next_first_normal - next_second_normal) / 2 + 256;
            next_second_normal =
                next_second_normal + (next_first_normal - next_second_normal) / 2 - 256;
        }
    }

    first->component_x = clamp_contact_component(
        rounded_pair(axis_sine, (int16_t)next_first_normal, axis_cosine, (int16_t)first_tangent));
    first->component_y = clamp_contact_component(
        rounded_pair(axis_sine, (int16_t)first_tangent, -axis_cosine, (int16_t)next_first_normal));
    second->component_x = clamp_contact_component(
        rounded_pair(axis_sine, (int16_t)next_second_normal, axis_cosine, (int16_t)second_tangent));
    second->component_y = clamp_contact_component(rounded_pair(
        axis_sine, (int16_t)second_tangent, -axis_cosine, (int16_t)next_second_normal));
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpRosterStatus update_initial_acceleration(PortableGpRacer *racer,
                                                          const PortableRaceRulesView *rules)
{
    const PortableGpCpuTuning *tuning = rules->cpu_tuning;
    if (tuning == NULL || rules->source_auto_deceleration_table == NULL ||
        rules->source_auto_deceleration_count != 8u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    uint32_t target_index =
        (uint32_t)(racer->rank_row_9a & 0x0fu) * 4u + (racer->acceleration_mode_19b & 3u);
    int32_t target;
    int32_t speed;
    uint32_t bin;

    if (target_index >= 20u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    target =
        (int32_t)tuning->target_speeds[racer->driver_class][target_index / 4u][target_index % 4u]
        << 4;
    target += racer->speed_target_bias_19d;
    target += (racer->coin_count_178 > 16u ? 16 : racer->coin_count_178) * 2;
    if (racer->battle_navigation_valid && target > racer->battle_speed_target)
        target = racer->battle_speed_target;
    speed = fixed_to_int(racer->speed_q16);
    if (racer->battle_navigation_valid && target == 0 && speed <= 0)
    {
        racer->acceleration_q16 = 0;
        return PORTABLE_GP_ROSTER_OK;
    }
    if (target - speed < 0)
    {
        uint32_t difference = (uint32_t)(speed - target);
        int32_t high;
        if (difference > 0x1ffu)
            difference = 0x1ffu;
        high = (int32_t)rules->source_auto_deceleration_table[difference >> 6] * INT32_C(0x10000);
        racer->acceleration_q16 = high | (int32_t)((uint32_t)racer->acceleration_q16 & 0xffffu);
        return PORTABLE_GP_ROSTER_OK;
    }
    if (speed > 0x3ff)
        speed = 0x3ff;
    bin = ((uint32_t)speed >> 6) & 0x0fu;
    racer->acceleration_q16 =
        (int32_t)tuning->acceleration[racer->driver_class][bin] * INT32_C(4096);
    return PORTABLE_GP_ROSTER_OK;
}

/* sub_803AE18 consumes the contact flag after the normal collision solver.
 * sub_803BD24 mode 11 then eases the signed angular offset and selects the
 * corresponding acceleration row once the contact hold has ended. */
static PortableGpRosterStatus update_kart_contact_recovery(PortableGpRacer *racer,
                                                           const PortableDriverRosterRules *rules)
{
    if (rules == NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    int16_t spin;
    uint32_t bin;
    if (racer->mushroom_mode_84 != 11u)
        return PORTABLE_GP_ROSTER_OK;
    if ((racer->source_flags & UINT32_C(0x4000)) != 0u)
        return PORTABLE_GP_ROSTER_OK;
    if (racer->speed_q16 <= INT32_C(0x00ffffff))
        racer->hit_spin_82 = 0;
    spin = racer->hit_spin_82;
    if (spin < 0)
    {
        spin = (int16_t)(uint16_t)((uint16_t)spin + UINT16_C(0x0400));
        if (spin >= 0)
        {
            racer->hit_spin_82 = spin;
            racer->mushroom_mode_84 = 0u;
            return PORTABLE_GP_ROSTER_OK;
        }
    }
    else if (spin > 0)
    {
        spin = (int16_t)(uint16_t)((uint16_t)spin - UINT16_C(0x0400));
        if (spin < 0)
        {
            racer->hit_spin_82 = spin;
            racer->mushroom_mode_84 = 0u;
            return PORTABLE_GP_ROSTER_OK;
        }
    }
    racer->hit_spin_82 = spin;
    if (spin == 0)
    {
        racer->mushroom_mode_84 = 0u;
        return PORTABLE_GP_ROSTER_OK;
    }
    bin = ((uint32_t)(spin < 0 ? -(int32_t)spin : spin) >> 11) & 7u;
    racer->acceleration_q16 = (int32_t)rules->recovery_acceleration[bin] * INT32_C(0x10000);
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpRosterStatus enter_kart_contact_recovery(PortableGpRacer *racer)
{
    uint16_t angle;
    if ((racer->source_flags & UINT32_C(0x1000)) == 0u)
        return PORTABLE_GP_ROSTER_OK;
    if (racer->component_x == 0 && racer->component_y == 0)
        return PORTABLE_GP_ROSTER_OK;
    if (portable_driver_vector_effect_source_angle(racer->component_x, racer->component_y,
                                                   &angle) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer->hit_spin_82 = (int16_t)(uint16_t)(angle - racer->heading);
    racer->mushroom_mode_84 = 11u;
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpRacer *racer_at_grid_rank(PortableGpRaceRoster *roster, uint16_t rank)
{
    for (size_t index = 0u; index < portable_gp_race_roster_count(roster); ++index)
        if (roster->racers[index].rank_flags_ba / 2u == rank)
            return &roster->racers[index];
    return NULL;
}

static int32_t rank_distance(const PortableGpRacer *ahead, const PortableGpRacer *behind,
                             size_t checkpoint_count)
{
    const int32_t wrap_ahead = (ahead->route_wrap_91 + 1u) & 7u;
    const int32_t wrap_behind = (behind->route_wrap_91 + 1u) & 7u;
    /* 41F18 reads global +0x38 (nrCheckpoints), not Peach's47. */
    return (int32_t)checkpoint_count * (wrap_ahead - wrap_behind) + (int32_t)ahead->route_index -
           behind->route_index;
}

static uint8_t initial_rank_row(PortableGpRaceRoster *roster, PortableGpRacer *racer,
                                size_t checkpoint_count)
{
    const uint8_t *last_grid_route_threshold = roster->roster_rules->pacing_route[0];
    const uint8_t *last_grid_progress_threshold = roster->roster_rules->pacing_progress[0][0];
    const uint16_t rank = racer->rank_flags_ba / 2u;
    PortableGpRacer *previous;
    PortableGpRacer *next;
    int32_t previous_delta;
    int32_t next_delta;
    int32_t previous_distance;
    int32_t next_distance;

    /* sub_8041F18 returns row 3 before dispatching by rank for racers
     * carrying the finish marker at driver +0x48. */
    if ((racer->source_flags & UINT32_C(0x20)) != 0u)
        return 3u;

    /* 41F88 suppresses rank-based pacing during input lock or shrinking.
     * Protected racers take the earlier 41F42 branch instead. */
    if (racer->lightning_status_6a != 0)
    {
        for (unsigned i = 0u; i < rank; ++i)
        {
            const PortableGpRacer *ahead = racer_at_grid_rank(roster, (uint16_t)i);
            if (ahead == NULL)
                return 3u;
            if ((ahead->source_flags & UINT32_C(0x8000)) != 0u)
                return 4u;
        }
        if (rank + 1u == portable_gp_race_roster_count(roster))
            return 2u;
        const PortableGpRacer *behind = racer_at_grid_rank(roster, (uint16_t)(rank + 1u));
        return behind != NULL && behind->route_index == racer->route_index ? 0u : 2u;
    }
    if (racer->item_lock_ac != 0u || racer->lightning_timer_68 != 0u)
        return 3u;

    if (rank == 0u)
    {
        PortableGpRacer *player = racer_at_grid_rank(roster, 7u);
        const uint8_t wrap = (racer->route_wrap_91 + 1u) & 7u;
        next = racer_at_grid_rank(roster, 1u);
        if (next == NULL || player == NULL)
            return 3u;
        if (next->is_player && racer->progress_ae > 6)
            return 1u;
        if (wrap == 1u)
            return racer->progress_ae > 2 ? 1u : 0u;
        if (wrap < 4u &&
            rank_distance(racer, player, checkpoint_count) >= last_grid_route_threshold[wrap])
            return 2u;
        if (racer->progress_ae >= last_grid_progress_threshold[0])
            return 1u;
        if (racer->progress_ae >= last_grid_progress_threshold[1])
            return 0u;
        return racer->progress_ae >= last_grid_progress_threshold[2] ? 2u : 3u;
    }
    if (rank >= 7u)
    {
        const uint8_t *peach_last_place_distance = roster->roster_rules->pacing_last_place;
        PortableGpRacer *leader;
        uint8_t wrap;
        int32_t leader_distance;
        uint8_t threshold;
        /* sub_8041E20 owns the last-place row.  Its close-pack branch compares
         * the racer with rank 6 before consulting the original leader-distance
         * table. The retained Peach GP is table row 0 (+0x7dc = 0). */
        previous = racer_at_grid_rank(roster, 6u);
        leader = racer_at_grid_rank(roster, 0u);
        if (previous == NULL || leader == NULL)
            return 3u;
        if (previous->is_player && racer->progress_ae > 3)
            return 1u;
        if (racer->progress_ae - previous->progress_ae > 0)
            return UINT8_C(0x41);
        if (rank_distance(previous, racer, checkpoint_count) <= 1)
            return 2u;
        wrap = (uint8_t)((racer->route_wrap_91 + 1u) & 7u);
        threshold = peach_last_place_distance[wrap];
        leader_distance = rank_distance(leader, racer, checkpoint_count);
        if (leader_distance >= threshold)
            return 1u;
        return leader_distance >= (int32_t)(threshold / 2u) ? 0u : 2u;
    }
    previous = racer_at_grid_rank(roster, rank - 1u);
    next = racer_at_grid_rank(roster, rank + 1u);
    if (previous == NULL || next == NULL)
        return 3u;
    previous_delta = racer->progress_ae - previous->progress_ae;
    next_delta = racer->progress_ae - next->progress_ae;
    if (next->is_player)
    {
        if (racer->progress_ae > 6)
            return 1u;
    }
    if (previous->is_player && racer->progress_ae > 3)
    {
        /* 41B78's player-ahead branch returns1 below the engine-zero
         * ECCE4 threshold12. It does not fall through to the generic
         * 41/82 progress-offset branches (A rival7 +1655). */
        return rank == 1u && rank_distance(previous, racer, checkpoint_count) >= 12 ? 4u : 1u;
    }
    if (previous_delta > 0)
        return UINT8_C(0x41);
    if (next_delta <= -4)
        return UINT8_C(0x82);
    previous_distance = rank_distance(previous, racer, checkpoint_count);
    next_distance = rank_distance(racer, next, checkpoint_count);
    if (previous_distance + previous_distance / 2 - next_distance >= 0)
        return 1u;
    if (previous_distance * 2 - next_distance <= 0)
        return 3u;
    return next_delta > 1 ? 1u : 0u;
}

/* One subtraction owner for item and kart contacts. Effects consume these
 * source-cause requests on the selected local driver's next FX pass. */
static void lose_racer_coins(PortableGpRacer *racer, uint8_t cause, uint16_t requested,
                             uint32_t clock)
{
    if (!racer->is_player || (racer->source_flags & UINT32_C(0x20)))
        return;
    const uint16_t loss = racer->coin_count_178 < requested ? racer->coin_count_178 : requested;
    racer->coin_count_178 = (uint16_t)(racer->coin_count_178 - loss);
    if (!loss)
        return;
    if (racer->coin_loss_event_count >= 8u)
    {
        racer->coin_loss_event_overflow = 1u;
        return;
    }
    const unsigned i = racer->coin_loss_event_count++;
    racer->coin_loss_events[i].cause = cause;
    racer->coin_loss_events[i].loss = (uint8_t)loss;
    racer->coin_loss_events[i].remaining = racer->coin_count_178;
    racer->coin_loss_events[i].clock = clock;
}

/* E490 and the E92C eight-tick contact entry. Eligibility belongs to the
 * caller: a second bomb has already entered mode9 when E92C damages it. */
static void apply_strong_kart_hit(PortableGpRacer *victim, uint32_t scene_clock,
                                  const uint32_t *ranked_flags, size_t ranked_count)
{
    victim->kart_contact_state_5a = UINT16_C(0x8007);
    victim->source_flags |= UINT32_C(0x1000);
    victim->banana_hit_timer_38 = 8u;
    victim->mushroom_response_flags_b4 |=
        (uint16_t)((scene_clock & 1u) != 0u ? UINT16_C(0x300) : UINT16_C(0x200));
    victim->hit_response_b6 =
        (victim->source_flags & UINT32_C(0x8000)) != 0u ? UINT16_C(0x1000) : UINT16_C(0x2000);
    for (size_t i = 0u; i < ranked_count; ++i)
        if ((ranked_flags[i] & UINT32_C(0x8000)) != 0u)
            victim->hit_response_b6 = UINT16_C(0x1000);
    victim->hit_state_flags_74 |= UINT16_C(3);
    lose_racer_coins(victim, 1u, 4u, scene_clock);
}

PortableGpRosterStatus
portable_gp_race_roster_begin_kart_hit(PortableGpRacer *victim, const PortableGpRacer *hitter,
                                       uint32_t scene_clock, const uint32_t *ranked_flags,
                                       size_t ranked_count, const int32_t *sine, size_t sine_count)
{
    if (victim == NULL || (hitter != NULL && (sine == NULL || sine_count < 1024u)) ||
        (ranked_count != 0u && ranked_flags == NULL))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    /* E92C EF70/EFD4: normal-size bit10 can hit a shrunken rival only
     * after the Star and unequal-bit40 branches have selected their response. */
    const int size_hit = hitter != NULL &&
        ((hitter->hit_cue_flags_58 | victim->hit_cue_flags_58) & UINT16_C(0x8000)) == 0u &&
        ((hitter->hit_cue_flags_58 ^ victim->hit_cue_flags_58) & UINT16_C(0x40)) == 0u &&
        (hitter->hit_cue_flags_58 & UINT16_C(0x10)) != 0u &&
        (victim->hit_cue_flags_58 & UINT16_C(0x10)) == 0u;
    const int hitter_allowed = hitter == NULL ||
        (hitter != victim && (size_hit ||
            (hitter->hit_cue_flags_58 & UINT16_C(0x8000)) != 0u ||
            ((hitter->hit_cue_flags_58 & UINT16_C(0x40)) != 0u &&
             (victim->hit_cue_flags_58 & UINT16_C(0x40)) == 0u)));
    /* AF10's route-direction bit400 does not guard F8C8/E490 damage. */
    if ((victim->source_flags & ~UINT32_C(0x1fc3f)) != 0u || victim->banana_hit_timer_38 > 1u ||
        victim->lightning_state_78 > 1u || victim->lightning_status_6a != 0 || ranked_count > 7u ||
        ranked_count != (victim->rank_flags_ba >> 1u) || !hitter_allowed)
        return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
    PortableGpRacer next = *victim;
    apply_strong_kart_hit(&next, scene_clock, ranked_flags, ranked_count);
    if (hitter != NULL)
    {
        /* E68C truncates world differences before angle selection, then
         * chooses the perpendicular to target7A on the victim's side. */
        const int32_t dx =
            signed32_bits((uint32_t)victim->position_x_q16 - (uint32_t)hitter->position_x_q16) /
            65536;
        const int32_t dy =
            signed32_bits((uint32_t)victim->position_y_q16 - (uint32_t)hitter->position_y_q16) /
            65536;
        uint16_t axis;
        if (portable_driver_vector_effect_source_angle(signed16((uint16_t)dx),
                                                       signed16((uint16_t)dy),
                                                       &axis) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        const uint16_t angle =
            (uint16_t)(hitter->heading + (signed16((uint16_t)(hitter->heading - axis)) >= 0
                                              ? UINT16_C(0xc000)
                                              : UINT16_C(0x4000)));
        const uint32_t index = impulse_sine_index(angle);
        next.component_x = signed16((uint16_t)((int64_t)sine[index] * 512 / 32768));
        next.component_y =
            signed16((uint16_t)(-(int64_t)sine[(256u - index) & UINT32_C(0x3ff)] * 512 / 32768));
    }
    *victim = next;
    return PORTABLE_GP_ROSTER_OK;
}

void portable_gp_race_roster_schedule_voice(PortableGpRaceRoster *roster, uint8_t racer,
                                            uint8_t ticks, uint8_t taunt)
{
    if (roster == NULL || racer >= 8u || roster->pending_voice_count >= 8u)
        return;
    roster->pending_voices[roster->pending_voice_count].racer = racer;
    roster->pending_voices[roster->pending_voice_count].ticks = ticks;
    roster->pending_voices[roster->pending_voice_count].taunt = taunt;
    ++roster->pending_voice_count;
}

PortableGpRosterStatus
portable_gp_race_roster_resolve_protected_kart_contact(PortableGpRaceRoster *roster, size_t first,
                                                       size_t second, uint32_t scene_clock,
                                                       const int32_t *sine, size_t sine_count)
{
    if (roster == NULL || first >= 8u || second >= 8u || first == second || sine == NULL ||
        sine_count < 1024u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const PortableGpRacer *a = &roster->racers[first];
    const PortableGpRacer *b = &roster->racers[second];
    const unsigned protected_a = (a->hit_cue_flags_58 & UINT16_C(0x8000)) != 0u;
    const unsigned protected_b = (b->hit_cue_flags_58 & UINT16_C(0x8000)) != 0u;
    if (!protected_a && !protected_b)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    /* F450 rejects guarded, airborne and recovering actors before E92C. */
    if (a->contact_status_60 != 0u || b->contact_status_60 != 0u || a->lightning_state_78 > 1u ||
        b->lightning_state_78 > 1u || a->contact_status_66 != 0 || b->contact_status_66 != 0 ||
        a->world_height_q16 > INT32_C(0x4ffff) || b->world_height_q16 > INT32_C(0x4ffff) ||
        (a->banana_hit_timer_38 | b->banana_hit_timer_38) > 1u)
        return PORTABLE_GP_ROSTER_OK;
    /* Two protected actors take F0E8, retaining their protection clocks. */
    if (!(protected_a && protected_b))
        note_player_bump(roster, first, second);
    if (protected_a && protected_b)
        return portable_gp_race_roster_begin_normal_kart_contact(roster, first, second, sine,
                                                                 sine_count);
    const size_t victim_index = protected_a ? second : first;
    const PortableGpRacer *hitter = protected_a ? a : b;
    const PortableGpRacer *victim = &roster->racers[victim_index];
    const unsigned count = victim->rank_flags_ba >> 1u;
    if (count > 7u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    uint32_t ranked_flags[7];
    unsigned seen = 0u;
    for (unsigned rank = 0u; rank < portable_gp_race_roster_count(roster); ++rank)
    {
        const unsigned owner = roster->standings[rank];
        if (owner >= 8u || (seen & (1u << owner)) != 0u)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        seen |= 1u << owner;
        if (rank < count)
            ranked_flags[rank] = roster->racers[owner].source_flags;
    }
    const PortableGpRosterStatus status = portable_gp_race_roster_begin_kart_hit(
        &roster->racers[victim_index], hitter, scene_clock, ranked_flags, count, sine, sine_count);
    if (status == PORTABLE_GP_ROSTER_OK)
    {
        ++roster->contact_events;
        /* E92C's one-sided star path starts EC934: wait 16, then ouch. */
        portable_gp_race_roster_schedule_voice(roster, (uint8_t)victim_index, 16u, 0u);
    }
    return status;
}

/* C4C0 updates76 independently of the terrain owner selected by9B38. */
static void cpu_sink_prefix(PortableGpRacer *racer)
{
    uint16_t position = (uint16_t)racer->hit_spin_82;
    portable_driver_sink_prefix(racer->lightning_state_78, &racer->fall_frames_76,
                                &racer->world_height_q16, &position);
    racer->hit_spin_82 = signed16(position);
}

typedef struct
{
    const PortableRaceRulesView *rules;
    const PortableTrackRaster *raster;
    const PortableTrackMaterialView *material;
    int32_t ramp_speed_limit_q16;
    const uint16_t *angle_lut;
    size_t angle_lut_count;
    uint8_t battle_enabled, battle_scene_state;
    const PortablePiranhaCourse *piranhas;
    const PortableCourseProps *course_props;
} CpuHitTerrain;

static PortableGpRosterStatus cpu_capture(const PortablePiranhaCourse *piranhas,
                                          PortableGpRacer *racer, uint8_t entry,
                                          const PortableRaceRulesView *rules,
                                          const PortableTrackRaster *raster,
                                          const uint16_t *angle_lut, size_t angle_lut_count,
                                          const PortableCourseProps *course_props);

static PortableGpRosterStatus cpu_pending_hit_motion(PortableGpRacer *racer, const int32_t *sine,
                                                     size_t sine_count,
                                                     const CpuHitTerrain *terrain)
{
    if (racer == NULL || sine == NULL || sine_count < 1024u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const int bomb_recovery = terrain != NULL && racer->elimination.revenge_bomb &&
                              racer->recovery_material == 0u &&
                              (racer->lightning_state_78 == 9u || racer->lightning_state_78 == 6u ||
                               racer->lightning_state_78 == 7u);
    /* A captured CPU keeps E490's +0xb4 0x300 spin through mode 8. */
    const int captured =
        terrain != NULL && racer->lightning_state_78 == PORTABLE_DRIVER_CAPTURE_MODE;
    if (racer->is_player ||
        (racer->lightning_state_78 > 1u && !(terrain != NULL && racer->lightning_state_78 == 4u) &&
         !bomb_recovery && !captured) ||
        (racer->lightning_state_78 == 0u &&
         (racer->world_height_q16 != 0 || racer->vertical_velocity_q8 != 0)) ||
        (racer->mushroom_mode_84 != 0u && racer->mushroom_mode_84 != 1u &&
         racer->mushroom_mode_84 != 4u && racer->mushroom_mode_84 != 8u &&
         racer->mushroom_mode_84 != 10u && racer->mushroom_mode_84 != 11u &&
         !(bomb_recovery && racer->mushroom_mode_84 == 2u)) ||
        (racer->mushroom_response_flags_b4 & UINT16_C(0x0200)) == 0u ||
        /*10000 is the independent held/flight item lock, retained through damage. */
        (racer->source_flags & ~UINT32_C(0x1783f)) != 0u)
        return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;

    PortableGpRacer next = *racer;
    /* C4C0 C63E..C66C derives contact cue bits from the incoming mode84,
     * before B550 and before BC00 changes it to10 below. In this qualified
     * mode0/10 branch bit20 is clear; bit40 clears on the NEXT hit turn.
     * Using the new mode would change the first hit's collision eligibility. */
    next.hit_cue_flags_58 = (uint16_t)((next.hit_cue_flags_58 & UINT16_C(0xff9f)) |
                                       (racer->mushroom_mode_84 == 8u ? UINT16_C(0x20) : 0u) |
                                       (racer->mushroom_mode_84 == 10u ? 0u : UINT16_C(0x0040)));
    next.source_flags &= ~UINT32_C(7);
    const uint8_t latched = (uint8_t)(next.banana_hit_timer_38 != 0u);
    if (latched)
    {
        --next.banana_hit_timer_38;
        next.source_flags |= UINT32_C(0x4000);
    }
    else if (captured)
    {
        next.source_flags |= UINT32_C(0x4000); /* 8040C3C re-arms +0x62. */
    }
    else
    {
        next.source_flags &= ~UINT32_C(0x4000);
        next.course_contact_hold = 0u;
    }
    cpu_sink_prefix(&next);
    if (terrain != NULL && terrain->battle_enabled)
        next.balloon_event = portable_battle_balloon_tick(
            &next.hit_state_flags_74, &next.balloons_remaining, terrain->battle_scene_state);
    const uint8_t hop_at_entry = next.lightning_state_78;
    /* 9B38 only consumes AE18/A9AC in its ground-entry branch. A58C
     * landing preserves the pending hit until the following driver tick. */
    if (hop_at_entry == 0u && (next.source_flags & UINT32_C(0x1000)) != 0u)
    {
        next.source_flags &= ~UINT32_C(0x1000);
        if (racer->component_x != 0 || racer->component_y != 0)
        {
            uint16_t angle;
            if (portable_driver_vector_effect_source_angle(racer->component_x, racer->component_y,
                                                           &angle) !=
                PORTABLE_DRIVER_VECTOR_EFFECT_OK)
                return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
            next.hit_spin_82 = signed16((uint16_t)(angle - racer->source_heading));
        }
    }
    if (hop_at_entry == 0u)
    {
        next.hit_state_7e = 13u;
        next.mushroom_mode_84 = 10u;
        next.hit_counter_88 = 0u;
    }
    if (captured)
    {
        const PortableGpRosterStatus status =
            cpu_capture(terrain->piranhas, &next, hop_at_entry, terrain->rules, terrain->raster,
                        terrain->angle_lut, terrain->angle_lut_count, terrain->course_props);
        if (status != PORTABLE_GP_ROSTER_OK)
            return status;
    }
    else if (terrain != NULL)
    {
        if (portable_gp_race_roster_terrain_tick(&next, terrain->rules, terrain->raster,
                                                 terrain->material, sine, sine_count,
                                                 terrain->ramp_speed_limit_q16, terrain->angle_lut,
                                                 terrain->angle_lut_count) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
    }
    else
    {
        PortableDriverHopState hop = {.height_q16 = next.world_height_q16,
                                      .state_flags = next.source_flags,
                                      .velocity_q8 = next.vertical_velocity_q8,
                                      .mode = hop_at_entry,
                                      .animation_flags = next.mushroom_response_flags_b4};
        if (portable_driver_hop_tick(&hop, 0u) != PORTABLE_DRIVER_HOP_OK)
            return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
        next.world_height_q16 = hop.height_q16;
        next.vertical_velocity_q8 = hop.velocity_q8;
        next.lightning_state_78 = hop.mode;
        next.source_flags = hop.state_flags;
        next.mushroom_response_flags_b4 = hop.animation_flags;
        next.terrain_jump_landed = hop.landed;
        if (hop.landed)
            (void)portable_driver_affine_request(&next.affine, next.affine_rules, 1u);
        if (hop.landed)
            next.mushroom_mode_84 = 0u;
    }
    if ((next.mushroom_response_flags_b4 & UINT16_C(0x0a00)) != 0u)
    {
        /* BC00 tests 0A00 in the ROM (the reference C's A000 is a
         * transcription error). BA98 runs its own halfword-crossing settlement,
         * independently of the local driver's 3956C dead zone. */
        int16_t step = signed16(next.hit_response_b6);
        if (step > INT16_C(0x1000))
            step = INT16_C(0x1000);
        const uint16_t spin = (uint16_t)next.hit_spin_82;
        next.hit_spin_82 =
            signed16((uint16_t)((next.mushroom_response_flags_b4 & UINT16_C(0x0100)) != 0u
                                    ? spin + (uint16_t)step
                                    : spin - (uint16_t)step));
        if (signed16(next.hit_response_b6) > 255)
            next.hit_response_b6 = (uint16_t)(next.hit_response_b6 - UINT16_C(0x0040));
        else
        {
            const int negative = next.hit_spin_82 < 0;
            next.hit_spin_82 =
                signed16((uint16_t)((uint16_t)next.hit_spin_82 +
                                    (negative ? UINT16_C(0x480) : (uint16_t)-UINT16_C(0x480))));
            if ((next.hit_spin_82 < 0) != negative)
            {
                next.mushroom_mode_84 = 0u;
                next.hit_spin_82 = 0;
                next.hit_angle_80 = 0u;
                next.hit_state_7e = 0u;
                next.mushroom_response_flags_b4 =
                    (uint16_t)((next.mushroom_response_flags_b4 & UINT16_C(0x40ff)) |
                               UINT16_C(0x400));
            }
        }
    }
    /* BD24 delegates recovery mode2 to8298 after BC00; a retained pending
     * hit may rotate first, but cannot accelerate or displace the return. */
    if (next.mushroom_mode_84 == 2u)
    {
        next.speed_q16 = 0;
        next.acceleration_q16 = 0;
        next.hit_counter_88 = 0u;
        next.hit_state_7e = 0u;
        next.hit_angle_80 = 0u;
        next.hit_spin_82 = 0;
    }
    next.source_heading = (uint16_t)(next.heading + (uint16_t)next.hit_spin_82);
    if (!latched && !captured)
    {
        PortableKartMotionState motion = {.speed_q16 = next.speed_q16,
                                          .acceleration_q16 = next.acceleration_q16,
                                          .angle = signed16(next.heading),
                                          .component_x = next.component_x,
                                          .component_y = next.component_y};
        if (portable_kart_motion_tick(&motion, sine, sine_count) != PORTABLE_KART_MOTION_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        next.speed_q16 = motion.speed_q16;
        next.component_x = motion.component_x;
        next.component_y = motion.component_y;
    }
    if (next.mushroom_mode_84 == 8u &&
        portable_driver_mushroom_tick(&next.mushroom_mode_84, &next.mushroom_timer_cc,
                                      &next.mushroom_response_flags_b4, &next.speed_q16,
                                      &next.acceleration_q16,
                                      next.mushroom_rules) != PORTABLE_DRIVER_MUSHROOM_OK)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (next.mushroom_mode_84 == 4u)
        next.acceleration_q16 = (int32_t)(uint16_t)next.acceleration_q16 +
                                (next.speed_q16 <= INT32_C(0x007bffff) ? 65536 : -65536);
    if (next.mushroom_mode_84 == 1u)
        next.acceleration_q16 = (int32_t)((uint32_t)next.acceleration_q16 & UINT32_C(0xffff));
    if (next.mushroom_mode_84 == 10u)
    {
        /*8298 writes the negative high half after integration. On settlement
         * BD24 instead runs normal acceleration in the roster's caller. */
        next.acceleration_q16 =
            -INT32_C(0x00380000) + (int32_t)((uint32_t)next.acceleration_q16 & UINT32_C(0xffff));
        if ((int64_t)next.speed_q16 + next.acceleration_q16 <= 0)
        {
            next.speed_q16 = 0;
            next.acceleration_q16 = 0;
        }
    }
    *racer = next;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_cpu_pending_hit_motion(PortableGpRacer *racer,
                                                                      const int32_t *sine,
                                                                      size_t sine_count)
{
    return cpu_pending_hit_motion(racer, sine, sine_count, NULL);
}

PortableGpRosterStatus portable_gp_race_roster_terrain_jump(PortableGpRacer *racer,
                                                            const int32_t *sine, size_t sine_count,
                                                            int32_t ramp_speed_limit_q16)
{
    if (racer == NULL || sine == NULL || sine_count < 1024u || ramp_speed_limit_q16 < 0)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer next = *racer;
    const uint8_t mode_at_entry = next.lightning_state_78;
    PortableDriverHopState hop = {
        .height_q16 = next.world_height_q16,
        .state_flags = next.source_flags,
        .velocity_q8 = next.vertical_velocity_q8,
        .mode = mode_at_entry,
        .animation_flags = next.mushroom_response_flags_b4,
    };
    /* CPU C4C0 calls the same9B38 jump consumer before8200. Its ramp cap
     * comes from course material data; source lap/contact flags do not gate
     * A118/A21C/A3A0/A448. Airborne turns advance A58C once and cannot launch
     * another surface jump on their landing turn. */
    if (portable_driver_hop_tick(&hop, 0u) != PORTABLE_DRIVER_HOP_OK ||
        portable_driver_surface_jump(&hop, mode_at_entry, next.source_material_85, &next.speed_q16,
                                     ramp_speed_limit_q16, sine,
                                     sine_count) != PORTABLE_DRIVER_HOP_OK)
        return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
    next.world_height_q16 = hop.height_q16;
    next.source_flags = hop.state_flags;
    next.vertical_velocity_q8 = hop.velocity_q8;
    next.lightning_state_78 = hop.mode;
    next.mushroom_response_flags_b4 = hop.animation_flags;
    next.terrain_jump_landed = hop.landed;
    if (hop.landed)
        (void)portable_driver_affine_request(&next.affine, next.affine_rules, 1u);
    if (hop.surface_jump_started)
        next.mushroom_mode_84 = 1u;
    else if (hop.landed && next.mushroom_mode_84 != 8u)
        next.mushroom_mode_84 = 0u;
    *racer = next;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_terrain_tick(
    PortableGpRacer *racer, const PortableRaceRulesView *rules, const PortableTrackRaster *raster,
    const PortableTrackMaterialView *material, const int32_t *sine, size_t sine_count,
    int32_t ramp_speed_limit_q16, const uint16_t *angle_lut, size_t angle_lut_count)
{
    if (racer == NULL || rules == NULL || raster == NULL || sine == NULL || sine_count < 1024u ||
        ramp_speed_limit_q16 < 0)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer next = *racer;
    const uint8_t entry = next.lightning_state_78;
    if (entry == 0u)
    {
        /* A9AC gives queued damage precedence over a mushroom. The
         * EC280 status gate retains B2 until the ground response permits it. */
        int16_t impact_angle = (int16_t)next.hit_angle_80;
        if (portable_driver_mushroom_consume(
                entry, &next.item_response_flags_b2, &next.mushroom_mode_84,
                &next.mushroom_timer_cc, &next.mushroom_response_flags_b4, &next.hit_state_7e,
                &impact_angle, next.mushroom_rules) != PORTABLE_DRIVER_MUSHROOM_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        next.hit_angle_80 = (uint16_t)impact_angle;
        if (next.source_material_85 >= 0x40u && next.source_material_85 < 0x80u)
            next.source_surface_selector_86 = (uint8_t)((next.source_material_85 >> 1u) & 15u);
        next.recovery_material = 0u;
        if (next.source_material_85 == UINT8_C(0x20) || next.source_material_85 == UINT8_C(0x22) ||
            next.source_material_85 == UINT8_C(0x24) || next.source_material_85 == UINT8_C(0x26))
            next.recovery_material = next.source_material_85;
    }
    if (entry <= 1u && (entry == 1u || next.recovery_material == 0u))
    {
        const PortableGpRosterStatus status =
            portable_gp_race_roster_terrain_jump(&next, sine, sine_count, ramp_speed_limit_q16);
        if (status != PORTABLE_GP_ROSTER_OK)
            return status;
        PortableDriverHopState hop = {.height_q16 = next.world_height_q16,
                                      .state_flags = next.source_flags,
                                      .velocity_q8 = next.vertical_velocity_q8,
                                      .mode = next.lightning_state_78,
                                      .animation_flags = next.mushroom_response_flags_b4};
        if (portable_driver_protected_surface_hop(
                &hop, entry, next.source_material_85, next.source_surface_selector_86,
                &next.speed_q16, material != NULL ? material->protected_hop_thresholds : NULL,
                material != NULL ? material->protected_hop_threshold_count : 0u) !=
            PORTABLE_DRIVER_HOP_OK)
            return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
        next.source_flags = hop.state_flags;
        next.vertical_velocity_q8 = hop.velocity_q8;
        next.lightning_state_78 = hop.mode;
        next.mushroom_response_flags_b4 = hop.animation_flags;
        if (entry == 0u && next.source_material_85 == 0x28u && hop.mode == 1u)
            next.hit_state_7e = 14u;
        if (portable_driver_surface_boost(entry, next.source_material_85, &next.mushroom_mode_84,
                                          &next.mushroom_timer_cc, &next.mushroom_response_flags_b4,
                                          &next.hit_state_7e, NULL) != PORTABLE_DRIVER_MUSHROOM_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        next.recovery_material = 0u;
        *racer = next;
        return PORTABLE_GP_ROSTER_OK;
    }
    PortableDriverHopState hop = {
        .height_q16 = next.world_height_q16,
        .state_flags = next.source_flags,
        .velocity_q8 = next.vertical_velocity_q8,
        .mode = entry,
        .animation_flags = next.mushroom_response_flags_b4,
    };
    int status;
    if (next.elimination.revenge_bomb && entry >= 2u &&
        (entry == 9u || next.recovery_material == 0u))
        status = portable_driver_bomb_respawn_tick(&hop, entry, &next.recovery_timer_9c);
    else
        switch (next.recovery_material)
        {
        case 0x20:
            /* A630 owns the hold; C4C0 already updated the sink prefix. */
            status = portable_driver_fall_tick(&hop, entry, &next.speed_q16,
                                               &next.recovery_timer_9c, &next.recovery_contact_34);
            break;
        case 0x22:
            status = portable_driver_terrain22_tick(&hop, entry, next.source_material_85,
                                                    &next.speed_q16, &next.recovery_timer_9c, sine,
                                                    sine_count);
            break;
        case 0x24:
            status =
                portable_driver_lava_tick(&hop, entry, &next.speed_q16, &next.hit_state_flags_74);
            break;
        case 0x26:
            status = portable_driver_bounds_tick(&hop, entry, &next.speed_q16);
            break;
        default:
            return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
        }
    if (status != PORTABLE_DRIVER_HOP_OK)
        return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
    if (entry == 6u || entry == 7u)
    {
        if (portable_driver_recovery_tick(
                &hop, &next.position_x_q16, &next.position_y_q16, &next.heading,
                &next.hit_state_flags_74, &next.recovery_contact_34, next.recovery_target_x_9e,
                next.recovery_target_y_a0,
                next.recovery_target_heading_a2) != PORTABLE_DRIVER_HOP_OK)
            return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
        next.source_heading = next.heading;
    }
    if (hop.retrieval_started || (entry == 4u && next.recovery_timer_9c <= 119u))
    {
        PortableDriverRecoveryTarget target;
        if (!portable_driver_recovery_target(rules->route, rules->route_count, next.route_index,
                                             raster, angle_lut, angle_lut_count, &target))
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
        next.recovery_target_x_9e = (int16_t)target.position.x;
        next.recovery_target_y_a0 = (int16_t)target.position.y;
        next.recovery_target_heading_a2 = target.heading;
    }
    /* 39F74 calls D214(3) for every kart; only its splash, sound and
     * ACFC smoke emitter are local. CPU terrain entries share the burn. */
    if (hop.recovery_started && next.recovery_material == 0x24u)
        next.burn_ticks_72 = PORTABLE_DRIVER_BURN_TICKS;
    if (hop.recovery_started && next.recovery_material == 0x20u)
    {
        ++next.fall_audio_serial;
        next.fall_audio_variant = (next.hit_cue_flags_58 & 0x10u) ? 0u : 1u;
    }
    if (hop.recovery_started || hop.retrieval_started)
    {
        next.acceleration_q16 = 0;
        next.hit_counter_88 = 0u;
        next.hit_state_7e = hop.mode == 4u ? 14u : 0u;
        next.hit_angle_80 = 0u;
        next.hit_spin_82 = 0;
        next.lightning_timer_68 = 0u;
        if (next.lightning_status_6a > 4)
        {
            next.lightning_status_6a = 4;
            hop.animation_flags &= UINT16_C(0x4000);
        }
        next.hit_cue_flags_58 |= UINT16_C(0x10);
        if (hop.retrieval_started)
            hop.animation_flags &= UINT16_C(0xf5ff);
    }
    if (entry == 3u || entry == 5u || entry == 6u || entry == 7u)
    {
        next.component_x = 0;
        next.component_y = 0;
        next.acceleration_q16 = 0;
    }
    next.world_height_q16 = hop.height_q16;
    next.source_flags = hop.state_flags;
    next.vertical_velocity_q8 = hop.velocity_q8;
    next.lightning_state_78 = hop.mode;
    next.mushroom_response_flags_b4 = hop.animation_flags;
    next.terrain_jump_landed = hop.landed;
    next.recovery_checkpoint_sample = hop.recovery_checkpoint_sample;
    next.mushroom_mode_84 = hop.mode == 6u || hop.mode == 7u
                                ? (next.recovery_material == 0x20u   ? 2u
                                   : next.recovery_material == 0x26u ? 5u
                                                                     : 3u)
                                : hop.mode;
    if (next.elimination.revenge_bomb && next.recovery_material == 0u && hop.mode >= 2u)
        next.mushroom_mode_84 = 2u;
    if (hop.mode <= 1u)
        next.recovery_material = 0u;
    *racer = next;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_initial_all(PortableGpRaceRoster *roster,
                                                                const PortableRaceRulesView *rules,
                                                                const PortableTrackRaster *raster,
                                                                const int32_t *sine,
                                                                size_t sine_count)
{
    return portable_gp_race_roster_tick_initial_all_terrain(roster, rules, raster, sine, sine_count,
                                                            NULL, NULL, NULL, 0u, 0, NULL, 0u, NULL,
                                                            NULL, NULL);
}

static PortableGpRosterStatus sample_cpu_material(PortableGpRacer *racer,
                                                  const PortableTerrainView *terrain,
                                                  const PortableTrackMaterialView *material,
                                                  const PortableRecoveredCoin *coins,
                                                  size_t coin_count)
{
    if (racer->world_height_q16 > INT32_C(0x40000))
        racer->source_material_85 = UINT8_C(0x40);
    else
    {
        uint8_t cell;
        const int status = portable_terrain_query8(terrain, racer->position_x_q16 / 65536,
                                                   racer->position_y_q16 / 65536, &cell);
        if (status == PORTABLE_TERRAIN_QUERY_OUT_OF_BOUNDS)
            racer->source_material_85 = UINT8_C(0x26);
        else if (status != PORTABLE_TERRAIN_QUERY_OK)
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
        else
        {
            /* Callers supply only active coin placements. */
            for (size_t coin = 0u; coin < coin_count; ++coin)
                if (racer->position_x_q16 / (8 * 65536) ==
                        coins[coin].position_x_q16 / (8 * 65536) &&
                    racer->position_y_q16 / (8 * 65536) == coins[coin].position_y_q16 / (8 * 65536))
                {
                    cell = UINT8_C(0xc0);
                    break;
                }
            if (portable_track_material_query(material, cell, &racer->source_material_85) != 0)
                return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
        }
    }
    racer->source_material_valid = 1u;
    return PORTABLE_GP_ROSTER_OK;
}

/* The occupied-tile callback belongs to this driver's B550 turn, so a
 * protected CPU can remove a prop before the following driver samples it. */
static PortableGpRosterStatus cpu_prop_tile_contact(PortableCourseProps *props,
                                                    PortableGpRacer *racer, const int32_t *sine,
                                                    size_t sine_count, uint8_t *bounced)
{
    const PortableCoursePropContactInput input = {.world_x = racer->position_x_q16 / 65536,
                                                  .world_y = racer->position_y_q16 / 65536,
                                                  .height_q16 = racer->world_height_q16,
                                                  .anchor_x = racer->contact_center_x,
                                                  .anchor_y = racer->contact_center_y,
                                                  .component_x = racer->component_x,
                                                  .component_y = racer->component_y,
                                                  .heading = racer->source_heading,
                                                  .timer = racer->banana_hit_timer_38,
                                                  .guard = racer->contact_status_66,
                                                  .protection = racer->lightning_status_6a,
                                                  .material = racer->source_material_85,
                                                  .hop_mode = racer->lightning_state_78};
    PortableCoursePropContactResult result;
    const PortableCoursePropContactKind kind =
        portable_course_prop_tile_contact(props, &input, sine, sine_count, &result);
    *bounced = (uint8_t)(kind == PORTABLE_COURSE_PROP_CONTACT_BOUNCE);
    if (kind == PORTABLE_COURSE_PROP_CONTACT_ERROR)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (*bounced)
    {
        racer->component_x = result.vector.resultant_x;
        racer->component_y = result.vector.resultant_y;
        racer->kart_contact_state_5a = UINT16_C(0x8007);
        racer->source_flags |= UINT32_C(0x1000);
        racer->banana_hit_timer_38 = 8u;
        racer->course_contact_hold = 0u;
        /* 9B38 selects AE18 for source flag2000 (ordinary CPUs), AD6C
         * otherwise. Airborne/pending-hit owners keep their own dispatch. */
        if (racer->lightning_state_78 == 0u &&
            !(racer->mushroom_response_flags_b4 & UINT16_C(0x0200)))
        {
            if (racer->source_flags & UINT32_C(0x2000))
            {
                uint16_t angle;
                PortableDriverVectorCollisionResult response;
                if (portable_driver_vector_effect_source_angle(racer->component_x,
                                                               racer->component_y, &angle) !=
                        PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
                    portable_driver_vector_effect_apply_collision(
                        racer->component_x, racer->component_y, angle, racer->contact_heading_56,
                        racer->source_flags, (uint16_t)racer->hit_spin_82, racer->mushroom_mode_84,
                        &response) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
                    return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
                racer->source_flags = response.source_flags;
                racer->hit_spin_82 = signed16(response.angle_delta);
                racer->mushroom_mode_84 = response.timer_value;
            }
            else
            {
                PortableDriverNormalCollisionResult response;
                if (portable_driver_vector_effect_apply_normal_collision_source_angle(
                        racer->component_x, racer->component_y, racer->source_heading,
                        racer->source_flags, racer->heading, racer->hit_angle_80, 0,
                        racer->speed_q16, racer->mushroom_mode_84, racer->hit_state_7e,
                        &response) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
                    return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
                racer->source_flags = response.source_flags;
                racer->hit_angle_80 = response.angle_delta_80;
                racer->mushroom_mode_84 = response.timer_value_84;
                racer->hit_state_7e = response.response_timer_7e;
                racer->speed_q16 = response.magnitude_state_bc;
            }
        }
    }
    return PORTABLE_GP_ROSTER_OK;
}

static PortableDriverCaptureState cpu_capture_state(const PortableGpRacer *racer)
{
    return (PortableDriverCaptureState){.x_q16 = racer->position_x_q16,
                                        .y_q16 = racer->position_y_q16,
                                        .height_q16 = racer->world_height_q16,
                                        .state_flags = racer->source_flags,
                                        .component_x = racer->component_x,
                                        .component_y = racer->component_y,
                                        .velocity_q8 = racer->vertical_velocity_q8,
                                        .mode = racer->lightning_state_78,
                                        .response_status_7e = racer->hit_state_7e,
                                        .hit_angle_80 = racer->hit_angle_80,
                                        .spin_82 = (uint16_t)racer->hit_spin_82,
                                        .driver_mode_84 = racer->mushroom_mode_84,
                                        .response_88 = racer->hit_counter_88,
                                        .timer_9c = racer->recovery_timer_9c,
                                        .animation_b4 = racer->mushroom_response_flags_b4,
                                        .spin_rate_b6 = racer->hit_response_b6,
                                        .speed_q16 = racer->speed_q16,
                                        .acceleration_q16 = racer->acceleration_q16};
}

/* 9B38 material 0x2A and hop mode 8 for a CPU. The capture re-arms the B550
 * hold every turn, so 8200 never replaces its pull; the launch turn's hold
 * releases one turn later. CPUs have no capture cues. */
static PortableGpRosterStatus cpu_capture(const PortablePiranhaCourse *piranhas,
                                          PortableGpRacer *racer, uint8_t entry,
                                          const PortableRaceRulesView *rules,
                                          const PortableTrackRaster *raster,
                                          const uint16_t *angle_lut, size_t angle_lut_count,
                                          const PortableCourseProps *course_props)
{
    PortableDriverCaptureState capture = cpu_capture_state(racer);
    if (entry == 0u)
    {
        PortableDriverCaptureActor actors[PORTABLE_COURSE_CAPTURE_ACTOR_CAPACITY];
        const size_t count = portable_course_capture_actors(piranhas, course_props, actors,
                                                            PORTABLE_COURSE_CAPTURE_ACTOR_CAPACITY);
        capture.animation_b4 &= UINT16_C(0x4000);
        if (portable_driver_capture_begin(&capture, racer->hit_cue_flags_58,
                                          racer->lightning_status_6a, racer->contact_status_66,
                                          actors, count) != PORTABLE_DRIVER_HOP_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        if (!capture.captured)
            return PORTABLE_GP_ROSTER_OK;
    }
    else if (portable_driver_capture_tick(&capture) != PORTABLE_DRIVER_HOP_OK)
        return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
    racer->world_height_q16 = capture.height_q16;
    racer->source_flags = capture.state_flags | UINT32_C(0x4000);
    racer->component_x = capture.component_x;
    racer->component_y = capture.component_y;
    racer->vertical_velocity_q8 = capture.velocity_q8;
    racer->lightning_state_78 = capture.mode;
    racer->hit_state_7e = capture.response_status_7e;
    racer->hit_angle_80 = capture.hit_angle_80;
    racer->hit_spin_82 = (int16_t)capture.spin_82;
    racer->mushroom_mode_84 = capture.driver_mode_84;
    racer->hit_counter_88 = capture.response_88;
    racer->recovery_timer_9c = capture.timer_9c;
    racer->mushroom_response_flags_b4 = capture.animation_b4;
    racer->hit_response_b6 = capture.spin_rate_b6;
    racer->speed_q16 = capture.speed_q16;
    racer->acceleration_q16 = capture.acceleration_q16;
    racer->recovery_material = 0u;
    if (capture.launched)
    {
        uint16_t heading;
        if (!portable_driver_course_heading(racer->position_x_q16, racer->position_y_q16,
                                            rules->route, rules->route_count, raster, angle_lut,
                                            angle_lut_count, &heading))
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
        racer->heading = racer->source_heading = heading;
    }
    return PORTABLE_GP_ROSTER_OK;
}

/* The launch turn's +0x62 holds 8200 for one more turn; 8E34 case 12 then
 * flips the airborne kart. */
static PortableGpRosterStatus cpu_launch_flip(PortableGpRacer *racer)
{
    if (racer->lightning_state_78 != 1u || racer->hit_state_7e != 12u)
        return PORTABLE_GP_ROSTER_OK;
    if ((racer->source_flags & UINT32_C(0x4000)) != 0u)
    {
        racer->course_contact_hold = 1u;
        return PORTABLE_GP_ROSTER_OK;
    }
    uint16_t spin = (uint16_t)racer->hit_spin_82;
    if (portable_driver_launch_flip_tick(&racer->hit_state_7e, &racer->mushroom_response_flags_b4,
                                         &spin,
                                         racer->vertical_velocity_q8) != PORTABLE_DRIVER_HOP_OK)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer->hit_spin_82 = (int16_t)spin;
    return PORTABLE_GP_ROSTER_OK;
}

/* Immutable batch inputs and observations retained only for one CPU turn.
 * Terminal hit/hold paths still publish through the caller's after_driver. */
typedef struct
{
    PortableGpRaceRoster *roster;
    const PortableRaceRulesView *rules;
    const PortableTrackRaster *raster;
    const int32_t *sine;
    size_t sine_count;
    const PortableTerrainView *terrain;
    const PortableTrackMaterialView *material;
    const PortableRecoveredCoin *coins;
    size_t coin_count;
    int32_t ramp_speed_limit_q16;
    const uint16_t *angle_lut;
    size_t angle_lut_count;
    PortableCourseProps *course_props;
} CpuRosterTick;

typedef struct
{
    const CpuRosterTick *tick;
    PortableGpRacer *racer;
    size_t index;
    uint8_t contact_hold, kart_contact_release, recovery_entry, complete;
} CpuRosterTurn;

static PortableGpRosterStatus cpu_turn_begin(CpuRosterTurn *turn)
{
    PortableGpRaceRoster *roster = turn->tick->roster;
    PortableGpRacer *racer = turn->racer;
    const size_t index = turn->index;
    if (portable_gp_race_roster_tick_protection(roster, index) != PORTABLE_GP_ROSTER_OK ||
        portable_gp_race_roster_tick_lightning_timer(roster, index) != PORTABLE_GP_ROSTER_OK)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if ((racer->rank_row_9a & 0x0fu) >= 5u || racer->driver_class >= 8u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer->balloon_event = PORTABLE_BATTLE_BALLOON_NONE;
    racer->terrain_jump_landed = 0u;
    racer->recovery_checkpoint_sample = 0u;
    racer->source_material_valid = 0u;
    if (!racer->decision_valid || (racer->rank_row_9a & 0x0fu) >= 5u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer->motion_active = 1u;
    portable_gp_race_roster_cache_contact_center(racer);
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpRosterStatus cpu_turn_pending_hit(CpuRosterTurn *turn)
{
    PortableGpRaceRoster *roster = turn->tick->roster;
    const PortableRaceRulesView *rules = turn->tick->rules;
    const PortableTrackRaster *raster = turn->tick->raster;
    const int32_t *sine = turn->tick->sine;
    const size_t sine_count = turn->tick->sine_count;
    const PortableTerrainView *terrain = turn->tick->terrain;
    const PortableTrackMaterialView *material = turn->tick->material;
    const PortableRecoveredCoin *coins = turn->tick->coins;
    const size_t coin_count = turn->tick->coin_count;
    const int32_t ramp_speed_limit_q16 = turn->tick->ramp_speed_limit_q16;
    const uint16_t *angle_lut = turn->tick->angle_lut;
    const size_t angle_lut_count = turn->tick->angle_lut_count;
    PortableCourseProps *course_props = turn->tick->course_props;
    PortableGpRacer *racer = turn->racer;
    const size_t index = turn->index;
    if (terrain != NULL && (racer->mushroom_response_flags_b4 & UINT16_C(0x0200)) != 0u)
    {
        /* C4C0 publishes preceding velocity and B98C progress before
         * B550/9B38 can land an airborne pending-hit victim. */
        racer->position_x_q16 = add_component(racer->position_x_q16, racer->component_x);
        racer->position_y_q16 = add_component(racer->position_y_q16, racer->component_y);
        if (portable_gp_race_roster_track_progress(racer, rules, raster, roster->ai_mode_row_c64) !=
            PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
        if (sample_cpu_material(racer, terrain, material, coins, coin_count) !=
            PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
        uint8_t tile_bounce;
        if (cpu_prop_tile_contact(course_props, racer, sine, sine_count, &tile_bounce) !=
            PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        const CpuHitTerrain hit_terrain = {
            rules,
            raster,
            material,
            ramp_speed_limit_q16,
            angle_lut,
            angle_lut_count,
            (uint8_t)((roster->race_mode_flags_7da & UINT16_C(0x800)) != 0u),
            roster->battle_scene_state,
            roster->piranhas,
            course_props};
        if (cpu_pending_hit_motion(racer, sine, sine_count, &hit_terrain) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
        if (racer->mushroom_mode_84 == 0u &&
            update_initial_acceleration(racer, rules) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;

        if (update_route_angle(racer, rules, raster, angle_lut, angle_lut_count) !=
            PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
        if (portable_gp_race_roster_tick_boo_timers(roster, index, 0u) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        racer->contact_heading_56 = racer->source_heading;
        if (roster->cpu_items_enabled && portable_gp_race_roster_use_cpu_item(
                                             roster, index, rules->route_count, rules->lap_count,
                                             sine, sine_count) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        if (update_standings_for_racer(roster, index, 0u) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        turn->complete = 1u;
    }
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpRosterStatus cpu_turn_integrate(CpuRosterTurn *turn)
{
    PortableGpRaceRoster *roster = turn->tick->roster;
    const PortableRaceRulesView *rules = turn->tick->rules;
    const PortableTrackRaster *raster = turn->tick->raster;
    PortableGpRacer *racer = turn->racer;
    /* C63E publishes the incoming boost mode before A9AC can
     * activate or expire it; E424 uses bit20 for contact weight. */
    racer->hit_cue_flags_58 = (uint16_t)((racer->hit_cue_flags_58 & UINT16_C(0xffdf)) |
                                         (racer->mushroom_mode_84 == 8u ? UINT16_C(0x20) : 0u));
    turn->contact_hold = (uint8_t)(racer->banana_hit_timer_38 != 0u &&
                                   racer->kart_contact_state_5a == UINT16_C(0x8007));
    /* sub_803B550 clears the contact hold before sub_803BD24 runs
     * mode 11. A zero offset exits the mode with prior acceleration. */
    turn->kart_contact_release = (uint8_t)(!turn->contact_hold && !racer->course_contact_hold &&
                                           racer->kart_contact_state_5a == UINT16_C(0x8007) &&
                                           (racer->source_flags & UINT32_C(0x4000)) != 0u);
    if (turn->kart_contact_release || (!turn->contact_hold && racer->course_contact_hold))
    {
        racer->source_flags &= ~UINT32_C(0x4000);
        racer->course_contact_hold = 0u;
    }
    racer->position_x_q16 = add_component(racer->position_x_q16, racer->component_x);
    racer->position_y_q16 = add_component(racer->position_y_q16, racer->component_y);
    /* C4C0/B98C observes the integrated pose exactly once, ahead
     * of9B38's carry/descent and BC00's steering. */
    if (portable_gp_race_roster_track_progress(racer, rules, raster, roster->ai_mode_row_c64) !=
        PORTABLE_GP_ROSTER_OK)
        return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
    if ((roster->race_mode_flags_7da & UINT16_C(0x800)) != 0u)
        racer->balloon_event = portable_battle_balloon_tick(
            &racer->hit_state_flags_74, &racer->balloons_remaining, roster->battle_scene_state);
    cpu_sink_prefix(racer);
    turn->recovery_entry = racer->lightning_state_78;
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpRosterStatus cpu_turn_recovery(CpuRosterTurn *turn)
{
    PortableGpRaceRoster *roster = turn->tick->roster;
    const PortableRaceRulesView *rules = turn->tick->rules;
    const PortableTrackRaster *raster = turn->tick->raster;
    const int32_t *sine = turn->tick->sine;
    const size_t sine_count = turn->tick->sine_count;
    const PortableTerrainView *terrain = turn->tick->terrain;
    const PortableTrackMaterialView *material = turn->tick->material;
    const PortableRecoveredCoin *coins = turn->tick->coins;
    const size_t coin_count = turn->tick->coin_count;
    const int32_t ramp_speed_limit_q16 = turn->tick->ramp_speed_limit_q16;
    const uint16_t *angle_lut = turn->tick->angle_lut;
    const size_t angle_lut_count = turn->tick->angle_lut_count;
    PortableCourseProps *course_props = turn->tick->course_props;
    PortableGpRacer *racer = turn->racer;
    if (terrain != NULL)
    {
        if (sample_cpu_material(racer, terrain, material, coins, coin_count) !=
            PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
        uint8_t tile_bounce;
        if (cpu_prop_tile_contact(course_props, racer, sine, sine_count, &tile_bounce) !=
            PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        if (tile_bounce)
        {
            turn->contact_hold = 1u;
            turn->kart_contact_release = 0u;
        }
        if (cpu_launch_flip(racer) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        if (turn->recovery_entry == PORTABLE_DRIVER_CAPTURE_MODE)
        {
            const PortableGpRosterStatus captured =
                cpu_capture(roster->piranhas, racer, turn->recovery_entry, rules, raster, angle_lut,
                            angle_lut_count, course_props);
            if (captured != PORTABLE_GP_ROSTER_OK)
                return captured;
        }
        else
        {
            if (portable_gp_race_roster_terrain_tick(racer, rules, raster, material, sine,
                                                     sine_count, ramp_speed_limit_q16, angle_lut,
                                                     angle_lut_count) != PORTABLE_GP_ROSTER_OK)
                return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
            if (turn->recovery_entry == 0u && racer->lightning_state_78 == 0u &&
                racer->source_material_85 == PORTABLE_DRIVER_CAPTURE_MATERIAL)
            {
                const PortableGpRosterStatus captured =
                    cpu_capture(roster->piranhas, racer, turn->recovery_entry, rules, raster,
                                angle_lut, angle_lut_count, course_props);
                if (captured != PORTABLE_GP_ROSTER_OK)
                    return captured;
            }
        }
        /* A800/A8EE calls B98C again only after the unsnapped Y leg.
         * Its updated boundary flags must precede BC84's steering. */
        if (racer->recovery_checkpoint_sample &&
            portable_gp_race_roster_track_progress(racer, rules, raster, roster->ai_mode_row_c64) !=
                PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
    }
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpRosterStatus cpu_turn_contact_hold(CpuRosterTurn *turn)
{
    PortableGpRaceRoster *roster = turn->tick->roster;
    const PortableRaceRulesView *rules = turn->tick->rules;
    const PortableTrackRaster *raster = turn->tick->raster;
    const int32_t *sine = turn->tick->sine;
    const size_t sine_count = turn->tick->sine_count;
    const uint16_t *angle_lut = turn->tick->angle_lut;
    const size_t angle_lut_count = turn->tick->angle_lut_count;
    PortableGpRacer *racer = turn->racer;
    const size_t index = turn->index;
    /* C4C0 still calls9B38 on contact holds. Only the normal
     * contact response is replaced when recovery owns this driver. */
    if (turn->contact_hold)
    {
        if (racer->banana_hit_timer_38 != 0u)
            --racer->banana_hit_timer_38;
        racer->source_flags = (racer->source_flags & ~UINT32_C(0x1000)) | UINT32_C(0x4000);
        const uint8_t course_hold = racer->course_contact_hold;
        if (racer->flattened_64 && racer->banana_hit_timer_38 == UINT16_C(0x6f80))
        {
            racer->flattened_64 = 0u;
            racer->banana_hit_timer_38 = 0u;
            (void)portable_driver_affine_request(&racer->affine, racer->affine_rules, 7u);
        }
        if (racer->recovery_material == 0u && turn->recovery_entry <= 1u)
        {
            if (!course_hold && enter_kart_contact_recovery(racer) != PORTABLE_GP_ROSTER_OK)
                return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
            racer->heading = approach_initial_heading(racer->heading, racer->route_angle,
                                                      racer->rank_row_9a, rules->cpu_tuning);
            racer->source_heading = racer->heading;
            /* BD24 still updates C0 while B550 suppresses8200. */
            if (course_hold && racer->mushroom_mode_84 == 0u &&
                update_initial_acceleration(racer, rules) != PORTABLE_GP_ROSTER_OK)
                return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
            if (update_route_angle(racer, rules, raster, angle_lut, angle_lut_count) !=
                PORTABLE_GP_ROSTER_OK)
                return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
            if (portable_gp_race_roster_tick_boo_timers(roster, index, 0u) != PORTABLE_GP_ROSTER_OK)
                return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
            racer->contact_heading_56 = racer->source_heading;
            if (roster->cpu_items_enabled &&
                portable_gp_race_roster_use_cpu_item(roster, index, rules->route_count,
                                                     rules->lap_count, sine,
                                                     sine_count) != PORTABLE_GP_ROSTER_OK)
                return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
            if (update_standings_for_racer(roster, index, 0u) != PORTABLE_GP_ROSTER_OK)
                return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
            turn->complete = 1u;
        }
    }
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpRosterStatus cpu_turn_motion(CpuRosterTurn *turn)
{
    const PortableRaceRulesView *rules = turn->tick->rules;
    const int32_t *sine = turn->tick->sine;
    const size_t sine_count = turn->tick->sine_count;
    PortableGpRacer *racer = turn->racer;
    if (turn->recovery_entry != 6u && turn->recovery_entry != 7u)
        racer->heading = approach_initial_heading(racer->heading, racer->route_angle,
                                                  racer->rank_row_9a, rules->cpu_tuning);
    racer->source_heading = racer->heading;
    PortableKartMotionState motion = (PortableKartMotionState){
        .speed_q16 = racer->speed_q16,
        .acceleration_q16 = racer->acceleration_q16,
        .angle = (int16_t)racer->heading,
        .component_x = racer->component_x,
        .component_y = racer->component_y,
    };
    if ((racer->source_flags & UINT32_C(0x4000)) == 0u &&
        portable_kart_motion_tick(&motion, sine, sine_count) != PORTABLE_KART_MOTION_OK)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer->speed_q16 = motion.speed_q16;
    racer->component_x = motion.component_x;
    racer->component_y = motion.component_y;
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpRosterStatus cpu_turn_publish(CpuRosterTurn *turn)
{
    PortableGpRaceRoster *roster = turn->tick->roster;
    const PortableRaceRulesView *rules = turn->tick->rules;
    const PortableTrackRaster *raster = turn->tick->raster;
    const int32_t *sine = turn->tick->sine;
    const size_t sine_count = turn->tick->sine_count;
    const PortableTerrainView *terrain = turn->tick->terrain;
    const uint16_t *angle_lut = turn->tick->angle_lut;
    const size_t angle_lut_count = turn->tick->angle_lut_count;
    PortableGpRacer *racer = turn->racer;
    const size_t index = turn->index;
    if (update_route_angle(racer, rules, raster, angle_lut, angle_lut_count) !=
        PORTABLE_GP_ROSTER_OK)
        return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
    if (racer->mushroom_mode_84 == 2u || racer->mushroom_mode_84 == 3u ||
        racer->mushroom_mode_84 == 5u)
    {
        /* BD24 delegates these modes to8298/384D8 after8200. */
        racer->speed_q16 = 0;
        racer->acceleration_q16 = 0;
        racer->hit_counter_88 = 0u;
        racer->hit_state_7e = 0u;
        racer->hit_angle_80 = 0u;
        racer->hit_spin_82 = 0;
    }
    else if (racer->mushroom_mode_84 == 8u)
    {
        /* A2B4's surface turbo shares BD24/8298 mode8 with items.
         * This dispatch follows8200 and never also runs normal AI
         * acceleration, including the timer-expiry turn. */
        if (portable_driver_mushroom_tick(&racer->mushroom_mode_84, &racer->mushroom_timer_cc,
                                          &racer->mushroom_response_flags_b4, &racer->speed_q16,
                                          &racer->acceleration_q16,
                                          racer->mushroom_rules) != PORTABLE_DRIVER_MUSHROOM_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    }
    else if (racer->mushroom_mode_84 == 4u)
    {
        /* BD24 supplies A.384A4 keeps C0's fraction and ±1 high. */
        racer->acceleration_q16 = (int32_t)(uint16_t)racer->acceleration_q16 +
                                  (racer->speed_q16 <= INT32_C(0x007bffff) ? 65536 : -65536);
    }
    else if (racer->mushroom_mode_84 == 11u || turn->kart_contact_release)
    {
        if (update_kart_contact_recovery(racer, rules->roster_rules) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    }
    else if (terrain != NULL && racer->mushroom_mode_84 == 1u)
        racer->acceleration_q16 = (int32_t)((uint32_t)racer->acceleration_q16 & 65535u);
    else if (update_initial_acceleration(racer, rules) != PORTABLE_GP_ROSTER_OK)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (portable_gp_race_roster_tick_boo_timers(roster, index, 0u) != PORTABLE_GP_ROSTER_OK)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer->contact_heading_56 = racer->source_heading;
    if (roster->cpu_items_enabled &&
        portable_gp_race_roster_use_cpu_item(roster, index, rules->route_count, rules->lap_count,
                                             sine, sine_count) != PORTABLE_GP_ROSTER_OK)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (update_standings_for_racer(roster, index, 0u) != PORTABLE_GP_ROSTER_OK)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpRosterStatus cpu_turn_advance(CpuRosterTurn *turn)
{
    PortableGpRosterStatus status = cpu_turn_begin(turn);
    if (status != PORTABLE_GP_ROSTER_OK)
        return status;
    status = cpu_turn_pending_hit(turn);
    if (status != PORTABLE_GP_ROSTER_OK || turn->complete)
        return status;
    status = cpu_turn_integrate(turn);
    if (status != PORTABLE_GP_ROSTER_OK)
        return status;
    status = cpu_turn_recovery(turn);
    if (status != PORTABLE_GP_ROSTER_OK)
        return status;
    status = cpu_turn_contact_hold(turn);
    if (status != PORTABLE_GP_ROSTER_OK || turn->complete)
        return status;
    status = cpu_turn_motion(turn);
    if (status != PORTABLE_GP_ROSTER_OK)
        return status;
    return cpu_turn_publish(turn);
}

static void cpu_roster_publish_rank_row(PortableGpRaceRoster *roster,
                                        const PortableRaceRulesView *rules)
{
    /* The rotating sub_803DA54/sub_8042FFC row producer sees the standings
     * after the individual driver updates. */
    if (roster->cpu_items_enabled && roster->cpu_item_cooldown_c62 != 0u &&
        (roster->racers[roster->cpu_item_clock % portable_gp_race_roster_count(roster)]
             .source_flags &
         UINT32_C(0x2020)) != 0u)
        --roster->cpu_item_cooldown_c62;
    if (roster->rank_cursor != 0u && (roster->race_mode_flags_7da & UINT16_C(0x0800)) == 0u)
    {
        PortableGpRacer *ranked = &roster->racers[roster->rank_cursor];
        ranked->rank_row_9a = initial_rank_row(roster, ranked, rules->route_count);
    }
    roster->rank_cursor =
        (uint8_t)((roster->rank_cursor + 1u) % portable_gp_race_roster_count(roster));
    ++roster->authoritative_ticks;
}

PortableGpRosterStatus portable_gp_race_roster_tick_initial_all_terrain(
    PortableGpRaceRoster *roster, const PortableRaceRulesView *rules,
    const PortableTrackRaster *raster, const int32_t *sine, size_t sine_count,
    const PortableTerrainView *terrain, const PortableTrackMaterialView *material,
    const PortableRecoveredCoin *coins, size_t coin_count, int32_t ramp_speed_limit_q16,
    const uint16_t *angle_lut, size_t angle_lut_count, PortableCourseProps *course_props,
    PortableGpAfterDriver after_driver, void *after_driver_context)
{
    if (roster == NULL || rules == NULL || rules->cpu_tuning == NULL ||
        rules->roster_rules == NULL || raster == NULL || sine == NULL || sine_count < 1024u ||
        ramp_speed_limit_q16 < 0 || roster->course_id != rules->compatibility.course_id ||
        ((terrain == NULL) != (material == NULL)) || (coin_count != 0u && coins == NULL))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    /* The player has already moved in the session. sub_804AF3C runs the
     * standings insertion for each driver, rather than batching all eight
     * after their route updates. */
    for (size_t index = 0u; index < portable_gp_race_roster_count(roster); ++index)
        if (roster->racers[index].is_player &&
            update_standings_for_racer(roster, index, 0u) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const CpuRosterTick tick = {.roster = roster,
                                .rules = rules,
                                .raster = raster,
                                .sine = sine,
                                .sine_count = sine_count,
                                .terrain = terrain,
                                .material = material,
                                .coins = coins,
                                .coin_count = coin_count,
                                .ramp_speed_limit_q16 = ramp_speed_limit_q16,
                                .angle_lut = angle_lut,
                                .angle_lut_count = angle_lut_count,
                                .course_props = course_props};
    for (size_t index = 0u; index < portable_gp_race_roster_count(roster); ++index)
    {
        PortableGpRacer *racer = &roster->racers[index];
        racer->last_update_serial = ++roster->update_serial;
        if (racer->is_player)
            continue;
        CpuRosterTurn turn = {.tick = &tick, .racer = racer, .index = index};
        const PortableGpRosterStatus status = cpu_turn_advance(&turn);
        if (status != PORTABLE_GP_ROSTER_OK)
            return status;
        if (after_driver != NULL)
        {
            const PortableGpRosterStatus after = after_driver(after_driver_context, roster, index);
            if (after != PORTABLE_GP_ROSTER_OK)
                return after;
        }
    }
    cpu_roster_publish_rank_row(roster, rules);
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_apply_cup_ai_setup(PortableGpRaceRoster *roster,
                                                                  const uint8_t points[8],
                                                                  uint32_t course_position,
                                                                  uint8_t speed_class)
{

    uint8_t order[8];
    uint8_t seen = 0u;
    const uint8_t variant = (uint8_t)(course_position & 3u);
    int16_t base = 8;
    int16_t progress[8];
    uint8_t best_cpu = 0u;
    if (roster == NULL || points == NULL || speed_class >= 4u ||
        portable_gp_race_roster_count(roster) != PORTABLE_GP_RACER_COUNT ||
        !roster->racers[0].is_player)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    for (uint8_t racer = 0u; racer < 8u; ++racer)
    {
        const uint16_t rank = roster->racers[racer].rank_flags_ba >> 1u;
        if (rank >= 8u || (seen & (1u << rank)) != 0u ||
            (racer != 0u && roster->racers[racer].is_player))
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        seen = (uint8_t)(seen | (1u << rank));
        order[variant != 0u ? racer : rank] = racer;
    }
    /* 42DE0 later-race sort swaps only the first strict maximum. Equal
     * scores retain this traversal's ordering; no player tie preference. */
    if (variant != 0u)
        for (uint8_t first = 0u; first < 7u; ++first)
        {
            uint8_t best = first;
            for (uint8_t second = (uint8_t)(first + 1u); second < 8u; ++second)
                if (points[order[second]] > points[order[best]])
                    best = second;
            const uint8_t selected = order[best];
            order[best] = order[first];
            order[first] = selected;
        }
    for (uint8_t rank = 0u; rank < 8u; ++rank)
    {
        const uint8_t racer = order[rank];
        if (racer == 0u)
            progress[racer] = 4;
        else
        {
            progress[racer] = (int16_t)(base + (speed_class < 3u ? speed_class * 2u : 0u) +
                                        (rank <= 3u ? (variant != 0u ? variant : 1u) : 0u));
            if (base != 0)
                base -= 2;
            if (points[racer] > best_cpu)
                best_cpu = points[racer];
        }
    }
    const int difference = (int)points[0] - (int)best_cpu;
    roster->ai_mode_row_c64 = variant == 0u || difference <= 2 ? 0u : difference <= 8 ? 1u : 2u;
    for (uint8_t racer = 0u; racer < 8u; ++racer)
        roster->racers[racer].progress_ae = progress[racer];
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_init_for_rules(PortableGpRaceRoster *roster,
                                                              const PortableRaceRulesView *rules,
                                                              uint8_t selected_driver_class)
{
    if (roster == NULL || rules == NULL || rules->compatibility.course_id == 0u ||
        rules->roster_rules == NULL || selected_driver_class >= PORTABLE_GP_RACER_COUNT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;

    memset(roster, 0, sizeof *roster);
    roster->course_id = rules->compatibility.course_id;
    roster->roster_rules = rules->roster_rules;
    roster->rank_cursor = 3u;
    for (size_t index = 0u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        const size_t source = PORTABLE_GP_RACER_COUNT - 1u - index;
        const uint16_t grid_slot = (uint16_t)(14u - index * 2u);
        const uint32_t grid_rank = (uint32_t)(grid_slot / 2u);
        const int32_t x_offset =
            (grid_rank & 1u) != 0u ? INT32_C(20) << 16u : -(INT32_C(20) << 16u);
        const int32_t y_offset = (int32_t)(UINT32_C(8) + UINT32_C(24) * grid_rank) << 16u;
        PortableGpRacer *racer = &roster->racers[index];

        /* sub_8032AA0 selects the course grid row with rank_flags_ba / 2,
         * then sub_803D718 installs its x, y and heading.  The retained
         * Peach table establishes the source row layout around the recovered
         * course start: alternating -20/+20 x and 8+24*n y. */
        racer->position_x_q16 = (int32_t)((uint32_t)rules->start_x_q16 + (uint32_t)x_offset);
        racer->position_y_q16 = (int32_t)((uint32_t)rules->start_y_q16 + (uint32_t)y_offset);
        racer->heading = rules->start_heading;
        racer->source_heading = rules->start_heading;
        racer->contact_heading_56 = rules->start_heading;
        racer->source_flags = index == 0u ? UINT32_C(0x8000) : UINT32_C(0x2000);
        racer->grid_slot = grid_slot;
        racer->rank_flags_ba = grid_slot;
        roster->standings[grid_rank] = (uint8_t)index;
        racer->balloons_remaining = 3u;
        racer->driver_class = rules->roster_rules->rival_classes[selected_driver_class][source];
        racer->hit_cue_flags_58 = roster->roster_rules->contact_flags[racer->driver_class];
        racer->is_player = (uint8_t)(index == 0u);
        racer->recovery_contact_34 = UINT16_C(3);
        racer->progress_ae = 0;
        racer->route_wrap_91 = UINT8_C(127);
        racer->acceleration_mode_19b = (uint8_t)(index >= 1u && index <= 2u ? 2u : 0u);
        racer->coin_count_178 = 5u;
        racer->contact_axis_1e = 256;
        racer->contact_axis_20 = 256;
        racer->contact_height_24 = (uint16_t)(256u + index * 92u);
        if (index == 0u)
            racer->route_index = (uint8_t)rules->start_route_index;
        racer->max_progress_c6 = (uint16_t)((UINT16_C(127) << 8u) | racer->route_index);
    }
    /* Initial decisions precede the host's later-course point/rank setup.
     * Derive their original first-grid AE values through the same owner. */
    const uint8_t no_points[8] = {0};
    if (portable_gp_race_roster_apply_cup_ai_setup(roster, no_points, 0u, 0u) !=
        PORTABLE_GP_ROSTER_OK)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (rules->progress_kind == PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1)
    {
        roster->racer_count = 4u;
        roster->battle_scene_state = 3u;
        roster->race_mode_flags_7da = UINT16_C(0x0800);
        for (size_t index = 0u; index < PORTABLE_GP_RACER_COUNT; ++index)
        {
            roster->standings[index] = (uint8_t)index;
            roster->racers[index].rank_flags_ba = (uint16_t)(index * 2u);
            if (index >= 4u)
            {
                memset(&roster->racers[index], 0, sizeof roster->racers[index]);
                roster->racers[index].source_flags = UINT32_C(0x20);
                roster->racers[index].rank_flags_ba = (uint16_t)(index * 2u);
            }
        }
    }
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_set_locals(PortableGpRaceRoster *roster,
                                                          const uint8_t driver_classes[],
                                                          uint8_t count)
{
    if (roster == NULL || driver_classes == NULL || count < 2u || count > 4u ||
        count > portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    for (size_t index = 0u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        PortableGpRacer *racer = &roster->racers[index];
        roster->standings[index] = (uint8_t)index;
        if (index >= count)
        {
            memset(racer, 0, sizeof *racer);
            racer->source_flags = UINT32_C(0x20);
            racer->rank_flags_ba = (uint16_t)(index * 2u);
            continue;
        }
        if (driver_classes[index] >= PORTABLE_GP_RACER_COUNT || racer->rank_flags_ba != index * 2u)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        racer->driver_class = driver_classes[index];
        racer->hit_cue_flags_58 = roster->roster_rules->contact_flags[racer->driver_class];
        racer->is_player = 1u;
        racer->source_flags = UINT32_C(0x8000);
        racer->acceleration_mode_19b = 0u;
    }
    roster->racer_count = count;
    roster->local_racer = 0u;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_apply_course_grid(PortableGpRaceRoster *roster,
                                                                 const PortableGpGridRow *grid,
                                                                 const uint8_t *rank_by_racer)
{
    uint8_t seen = 0u;
    if (roster == NULL || grid == NULL || rank_by_racer == NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    for (size_t index = 0u; index < portable_gp_race_roster_count(roster); ++index)
    {
        const uint8_t rank = rank_by_racer[index];
        const uint8_t bit =
            rank < portable_gp_race_roster_count(roster) ? (uint8_t)(1u << rank) : 0u;
        if (bit == 0u || (seen & bit) != 0u || grid[rank].heading > UINT16_MAX)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        seen = (uint8_t)(seen | bit);
    }
    if (seen != (uint8_t)((1u << portable_gp_race_roster_count(roster)) - 1u))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    for (size_t index = 0u; index < portable_gp_race_roster_count(roster); ++index)
    {
        const uint8_t rank = rank_by_racer[index];
        PortableGpRacer *racer = &roster->racers[index];
        racer->position_x_q16 = grid[rank].x_q16;
        racer->position_y_q16 = grid[rank].y_q16;
        racer->heading = (uint16_t)grid[rank].heading;
        racer->source_heading = racer->heading;
        racer->contact_heading_56 = racer->source_heading;
        racer->grid_slot = (uint16_t)(rank * 2u);
        racer->rank_flags_ba = racer->grid_slot;
        roster->standings[rank] = (uint8_t)index;
    }
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpRosterStatus update_standings_for_racer(PortableGpRaceRoster *roster,
                                                         size_t racer_index, uint8_t compare_laps)
{
    PortableGpRacer *current;
    size_t rank;
    if (roster == NULL || racer_index >= portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    current = &roster->racers[racer_index];
    rank = (size_t)(current->rank_flags_ba >> 1u);
    if (rank >= portable_gp_race_roster_count(roster) || roster->standings[rank] != racer_index)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    while (rank != 0u)
    {
        const size_t previous_index = roster->standings[rank - 1u];
        PortableGpRacer *previous = &roster->racers[previous_index];
        uint32_t current_key;
        uint32_t previous_key;
        if (compare_laps || (roster->race_mode_flags_7da & UINT16_C(0x0f00)) == UINT16_C(0x0800))
        {
            current_key = current->balloons_remaining;
            previous_key = previous->balloons_remaining;
        }
        else
        {
            current_key = ((uint32_t)current->route_wrap_91 << 8u) | current->route_index;
            previous_key = ((uint32_t)previous->route_wrap_91 << 8u) | previous->route_index;
            if ((previous->source_flags & UINT32_C(0x20)) != 0u)
                break;
        }
        if (current_key <= previous_key)
            break;
        roster->standings[rank] = (uint8_t)previous_index;
        previous->rank_flags_ba = (uint16_t)(rank << 1u);
        --rank;
        roster->standings[rank] = (uint8_t)racer_index;
        current->rank_flags_ba = (uint16_t)(rank << 1u);
    }
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_update_standings(PortableGpRaceRoster *roster,
                                                                uint8_t compare_laps)
{
    if (roster == NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    for (size_t racer_index = 0u; racer_index < portable_gp_race_roster_count(roster);
         ++racer_index)
    {
        if (update_standings_for_racer(roster, racer_index, compare_laps) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    }
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_finish_transition(PortableGpRaceRoster *roster)
{
    if (roster == NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if ((roster->race_mode_flags_7da & UINT16_C(0x0f00)) == UINT16_C(0x0100))
    {
        /* sub_804A878's mode-0x100 phase-3 branch moves straight to
         * phase 4 after the player finishes; it does not mark rivals. */
        if ((roster->race_phase_7d4 & UINT8_C(0x0f)) == 3u)
        {
            if ((roster->racers[0].source_flags & UINT32_C(0x20)) == 0u)
                return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
            roster->race_phase_7d4 = (uint8_t)((roster->race_phase_7d4 & UINT8_C(0xf0)) | 4u);
            roster->finish_countdown = 0u;
            roster->finish_player_rank = (uint8_t)(roster->racers[0].rank_flags_ba >> 1u);
            return PORTABLE_GP_ROSTER_OK;
        }
        if ((roster->race_phase_7d4 & UINT8_C(0x0f)) == 4u)
        {
            if (roster->finish_countdown != 0u)
                --roster->finish_countdown;
            else
                roster->race_phase_7d4 = (uint8_t)((roster->race_phase_7d4 & UINT8_C(0xf0)) | 5u);
            return PORTABLE_GP_ROSTER_OK;
        }
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    }
    if ((roster->race_mode_flags_7da & UINT16_C(0x0f00)) != UINT16_C(0x0200))
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    if ((roster->race_phase_7d4 & UINT8_C(0x0f)) == 3u)
    {
        if ((roster->racers[0].source_flags & UINT32_C(0x20)) == 0u)
            return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
        for (size_t index = 1u; index < portable_gp_race_roster_count(roster); ++index)
            roster->racers[index].source_flags =
                (roster->racers[index].source_flags & UINT32_C(0xffffffcf)) | UINT32_C(0x00002030);
        roster->race_phase_7d4 = (uint8_t)((roster->race_phase_7d4 & UINT8_C(0xf0)) | 4u);
        roster->finish_countdown = 0u;
        roster->finish_player_rank = (uint8_t)(roster->racers[0].rank_flags_ba >> 1u);
        return PORTABLE_GP_ROSTER_OK;
    }
    if ((roster->race_phase_7d4 & UINT8_C(0x0f)) == 4u)
    {
        if (roster->finish_countdown != 0u)
            --roster->finish_countdown;
        else
            roster->race_phase_7d4 = (uint8_t)((roster->race_phase_7d4 & UINT8_C(0xf0)) | 5u);
        return PORTABLE_GP_ROSTER_OK;
    }
    return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
}

PortableGpRosterStatus portable_battle_race_roster_detonate_bomb(PortableGpRaceRoster *roster,
                                                                 size_t racer_index)
{
    if (roster == NULL || racer_index >= portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *racer = &roster->racers[racer_index];
    if ((roster->race_mode_flags_7da & UINT16_C(0x800)) == 0u || !racer->elimination.revenge_bomb ||
        racer->lightning_state_78 > 1u)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    if ((roster->racers[0].source_flags & UINT32_C(0x20)) == 0u)
    {
        racer->bomb_audio_volume = portable_gp_item_spatial_volume(
            racer->position_x_q16, racer->position_y_q16, roster->racers[0].position_x_q16,
            roster->racers[0].position_y_q16);
        racer->bomb_audio_pending = (uint8_t)(racer->bomb_audio_volume != 0u);
    }
    racer->recovery_contact_34 &= UINT16_C(0xfffc);
    racer->speed_q16 = 0;
    racer->acceleration_q16 = 0;
    racer->hit_counter_88 = 0u;
    racer->hit_angle_80 = 0u;
    racer->hit_spin_82 = 0;
    racer->recovery_target_x_9e = racer->arena_start_x;
    racer->recovery_target_y_a0 = racer->arena_start_y;
    racer->recovery_target_heading_a2 = racer->arena_start_heading;
    racer->recovery_timer_9c = 60u;
    racer->recovery_material = 0u;
    racer->world_height_q16 = INT32_C(0x100);
    racer->mushroom_mode_84 = 2u;
    racer->lightning_state_78 = 9u;
    racer->source_flags |= UINT32_C(0x800);
    racer->lightning_timer_68 = 0u;
    racer->hit_cue_flags_58 |= UINT16_C(0x10);
    racer->bomb_respawn_pending = racer->is_player;
    return PORTABLE_GP_ROSTER_OK;
}

/* Contact entry changes only its pair and the local bump latch. Keep a
 * bounded rollback owner so the second racer observes the first entry's
 * writes while a rejected contact still leaves the roster unchanged. */
typedef struct
{
    PortableGpRaceRoster *roster;
    size_t indices[2];
    PortableGpRacer racers[2];
    uint8_t player_bump_pending;
    int32_t player_bump_speed_q16;
} KartContactTransaction;

static KartContactTransaction begin_contact_transaction(PortableGpRaceRoster *roster,
                                                         size_t first, size_t second)
{
    return (KartContactTransaction){
        .roster = roster,
        .indices = {first, second},
        .racers = {roster->racers[first], roster->racers[second]},
        .player_bump_pending = roster->player_bump_pending,
        .player_bump_speed_q16 = roster->player_bump_speed_q16};
}

static PortableGpRosterStatus reject_contact_transaction(const KartContactTransaction *transaction,
                                                          PortableGpRosterStatus status)
{
    PortableGpRaceRoster *roster = transaction->roster;
    roster->racers[transaction->indices[0]] = transaction->racers[0];
    roster->racers[transaction->indices[1]] = transaction->racers[1];
    roster->player_bump_pending = transaction->player_bump_pending;
    roster->player_bump_speed_q16 = transaction->player_bump_speed_q16;
    return status;
}

PortableGpRosterStatus
portable_battle_race_roster_bomb_contact(PortableGpRaceRoster *roster, size_t first_index,
                                         size_t second_index, uint32_t scene_clock,
                                         const int32_t *sine, size_t sine_count, uint8_t *contacted)
{
    if (roster == NULL || contacted == NULL || sine == NULL || sine_count < 1024u ||
        first_index >= portable_gp_race_roster_count(roster) ||
        second_index >= portable_gp_race_roster_count(roster) || first_index == second_index)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    *contacted = 0u;
    const PortableGpRacer *a = &roster->racers[first_index], *b = &roster->racers[second_index];
    if ((roster->race_mode_flags_7da & UINT16_C(0x800)) == 0u ||
        !(a->elimination.revenge_bomb || b->elimination.revenge_bomb))
        return PORTABLE_GP_ROSTER_OK;
    /* F450's guards precede the bomb branch, even for a Star-protected kart. */
    const int64_t dx = (int64_t)a->position_x_q16 - b->position_x_q16;
    const int64_t dy = (int64_t)a->position_y_q16 - b->position_y_q16;
    if ((a->banana_hit_timer_38 | b->banana_hit_timer_38) > 1u ||
        (a->contact_status_60 | b->contact_status_60) != 0u || a->contact_status_66 != 0 ||
        b->contact_status_66 != 0 || a->lightning_state_78 > 1u || b->lightning_state_78 > 1u ||
        a->world_height_q16 > INT32_C(0x4ffff) || b->world_height_q16 > INT32_C(0x4ffff) ||
        dx < -INT32_C(0x50000) || dx > INT32_C(0x50000) || dy < -INT32_C(0x50000) ||
        dy > INT32_C(0x50000))
        return PORTABLE_GP_ROSTER_OK;
    const KartContactTransaction transaction =
        begin_contact_transaction(roster, first_index, second_index);
    PortableGpRaceRoster *next = roster;
    const size_t bomb_index = a->elimination.revenge_bomb ? first_index : second_index;
    const size_t victim_index = bomb_index == first_index ? second_index : first_index;
    PortableGpRacer *bomb = &next->racers[bomb_index], *victim = &next->racers[victim_index];
    if (portable_battle_race_roster_detonate_bomb(next, bomb_index) != PORTABLE_GP_ROSTER_OK)
        return reject_contact_transaction(&transaction, PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    if (victim->elimination.revenge_bomb &&
        portable_battle_race_roster_detonate_bomb(next, victim_index) != PORTABLE_GP_ROSTER_OK)
        return reject_contact_transaction(&transaction, PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    if ((victim->hit_cue_flags_58 & UINT16_C(0x8000)) == 0u)
    {
        const size_t rank = victim->rank_flags_ba >> 1u;
        if (rank >= portable_gp_race_roster_count(next))
            return reject_contact_transaction(&transaction, PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
        uint32_t ranked_flags[7];
        for (size_t i = 0u; i < rank; ++i)
        {
            if (next->standings[i] >= portable_gp_race_roster_count(next))
                return reject_contact_transaction(&transaction, PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
            ranked_flags[i] = next->racers[next->standings[i]].source_flags;
        }
        apply_strong_kart_hit(victim, scene_clock, ranked_flags, rank);
        /* E5A4(512,0): wrapped world deltas truncate toward zero; nonminimal
         * impulses rotate by a quarter-turn selected by the scene clock. */
        const int16_t x = signed16((uint16_t)(signed32_bits((uint32_t)victim->position_x_q16 -
                                                            (uint32_t)bomb->position_x_q16) /
                                              65536));
        const int16_t y = signed16((uint16_t)(signed32_bits((uint32_t)victim->position_y_q16 -
                                                            (uint32_t)bomb->position_y_q16) /
                                              65536));
        uint16_t angle = 0u;
        if ((x != 0 || y != 0) && portable_driver_vector_effect_source_angle(x, y, &angle) !=
                                      PORTABLE_DRIVER_VECTOR_EFFECT_OK)
            return reject_contact_transaction(&transaction, PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
        angle =
            (uint16_t)(angle + ((scene_clock & 1u) != 0u ? UINT16_C(0x4000) : UINT16_C(0xc000)));
        const uint32_t phase = impulse_sine_index(angle);
        victim->component_x = signed16((uint16_t)((int64_t)sine[phase] * 512 / 32768));
        victim->component_y =
            signed16((uint16_t)(-(int64_t)sine[(256u - phase) & 1023u] * 512 / 32768));
    }
    ++next->contact_events;
    *contacted = 1u;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_battle_race_roster_tick_balloons(PortableGpRaceRoster *roster,
                                                                 size_t racer_index,
                                                                 uint8_t scene_state,
                                                                 PortableBattleBalloonEvent *event)
{
    if (roster == NULL || racer_index >= portable_gp_race_roster_count(roster) || event == NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    *event = PORTABLE_BATTLE_BALLOON_NONE;
    /* C4C0 tests the Battle bit, rather than the finish routine's mode mask. */
    if ((roster->race_mode_flags_7da & UINT16_C(0x0800)) == 0u)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    PortableGpRacer *racer = &roster->racers[racer_index];
    *event = portable_battle_balloon_tick(&racer->hit_state_flags_74, &racer->balloons_remaining,
                                          scene_state);
    racer->balloon_event = *event;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus
portable_battle_race_roster_tick_finish_transition(PortableGpRaceRoster *roster)
{
    size_t nonzero = 0u;
    if (roster == NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if ((roster->race_phase_7d4 & UINT8_C(0x0f)) == 3u)
    {
        if ((roster->race_mode_flags_7da & UINT16_C(0x0f00)) != UINT16_C(0x0800))
            return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
        for (size_t index = 0u; index < portable_gp_race_roster_count(roster); ++index)
            if (roster->racers[index].balloons_remaining != 0u)
                ++nonzero;
        if (nonzero > 1u)
            return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
        for (size_t index = 0u; index < portable_gp_race_roster_count(roster); ++index)
        {
            roster->racers[index].source_flags |= UINT32_C(0x000008b8);
            roster->racers[index].speed_q16 = 0;
        }
        if (portable_gp_race_roster_update_standings(roster, 1u) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        roster->finish_result_delay_be4 = 8u;
        roster->race_phase_7d4 = (uint8_t)((roster->race_phase_7d4 & UINT8_C(0xf0)) | 4u);
        roster->finish_countdown = UINT16_C(0x00b4);
        roster->finish_player_rank = (uint8_t)(roster->racers[0].rank_flags_ba >> 1u);
        roster->finish_overlay_variant = (uint8_t)(roster->finish_player_rank != 0u);
        roster->finish_banner_occupied = 1u;
        roster->finish_banner_visible = 0u;
        roster->finish_banner_stage = 0u;
        roster->finish_banner_y = 63u;
        roster->finish_banner_affine_scale = UINT16_C(0x0100);
        roster->finish_banner_hold_counter = 0u;
        roster->finish_banner_value = 0u;
        return PORTABLE_GP_ROSTER_OK;
    }
    if ((roster->race_phase_7d4 & UINT8_C(0x0f)) == 4u)
    {
        if (roster->finish_countdown != 0u)
            --roster->finish_countdown;
        else
            roster->race_phase_7d4 = (uint8_t)((roster->race_phase_7d4 & UINT8_C(0xf0)) | 5u);
        return PORTABLE_GP_ROSTER_OK;
    }
    return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
}

PortableGpRosterStatus portable_battle_race_roster_tick_finish_banner(PortableGpRaceRoster *roster,
                                                                      const int32_t *sine,
                                                                      size_t sine_count)
{
    uint32_t value;
    int32_t angle;
    int32_t sample;
    int32_t delta;
    if (roster == NULL || sine == NULL || sine_count < 1024u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (!roster->finish_banner_occupied)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    if (roster->finish_banner_stage == 0u)
    {
        roster->finish_banner_visible = 1u;
        roster->finish_banner_stage = 1u;
        return PORTABLE_GP_ROSTER_OK;
    }
    if (roster->finish_banner_stage == 2u)
    {
        if (roster->finish_banner_hold_counter != 0u)
            --roster->finish_banner_hold_counter;
        else
        {
            roster->finish_banner_value = 0u;
            roster->finish_banner_stage = 3u;
        }
        return PORTABLE_GP_ROSTER_OK;
    }
    value = roster->finish_banner_value;
    angle = (int16_t)(uint16_t)value;
    if (angle < 0)
        angle += 0x3f;
    sample = sine[(uint16_t)angle >> 6u];
    if (sample < 0)
        sample += 0x1fff;
    delta = sample >= 0 ? sample >> 13u
                        : -(int32_t)(((uint32_t)(-(int64_t)sample) + UINT32_C(0x1fff)) >> 13u);
    roster->finish_banner_y = (uint16_t)(roster->finish_banner_y - delta);
    if (roster->finish_banner_stage == 1u)
    {
        if (value > UINT32_C(0xffff))
        {
            roster->finish_banner_hold_counter = UINT16_C(0x0078);
            roster->finish_banner_stage = 2u;
        }
        roster->finish_banner_affine_scale = (uint16_t)(UINT32_C(0x0200) - (value >> 8u));
    }
    else if (roster->finish_banner_stage == 3u)
    {
        if (value > UINT32_C(0xffff))
        {
            roster->finish_banner_occupied = 0u;
            roster->finish_banner_visible = 0u;
        }
        roster->finish_banner_affine_scale = (uint16_t)((value >> 8u) + UINT32_C(0x0100));
    }
    else
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    roster->finish_banner_value = value + UINT32_C(0x0800);
    return PORTABLE_GP_ROSTER_OK;
}

static int routes_touch(uint8_t first, uint8_t second, size_t route_count)
{
    if (route_count == 0u || first >= route_count || second >= route_count)
        return 0;
    return first == second || (size_t)((first + 1u) % route_count) == second ||
           (size_t)((second + 1u) % route_count) == first;
}

static void apply_contact_reaction(PortableGpRacer *racer, uint8_t mode,
                                   const PortableDriverRosterRules *rules)
{
    if (rules == NULL || racer->contact_height_24 > UINT16_C(0x1ff) ||
        (racer->contact_mode_6e & UINT8_C(0x0f)) != 0u)
        return;
    racer->contact_mode_6e = (uint8_t)((racer->contact_mode_6e & UINT8_C(0xf0)) | mode);
    racer->contact_phase_6f =
        (uint8_t)((racer->contact_phase_6f & 1u) | (rules->contact_script[0][1] << 1u));
    if (mode == 9u)
        racer->contact_flags_6d |= 1u;
    else
        racer->contact_flags_6d &= UINT8_C(0xfd);
}

PortableGpRosterStatus portable_gp_race_roster_resolve_initial_contact(PortableGpRaceRoster *roster,
                                                                       size_t route_count)
{
    PortableGpRacer *actor;
    PortableGpRacer *best = NULL;
    uint16_t best_delta = UINT16_C(0x1000);

    if (roster == NULL || route_count == 0u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    actor = &roster->racers[0];
    for (size_t index = 1u; index < portable_gp_race_roster_count(roster); ++index)
    {
        PortableGpRacer *candidate = &roster->racers[index];
        int32_t delta;
        if (!routes_touch(actor->route_index, candidate->route_index, route_count))
            continue;
        /* The source handles the opposite ordering immediately. The captured
         * first pass uses this nearest-behind path for every candidate. */
        if (actor->contact_axis_1e < candidate->contact_axis_1e)
            continue;
        delta = actor->contact_axis_1e - candidate->contact_axis_1e;
        if ((uint32_t)delta < best_delta)
        {
            best_delta = (uint16_t)delta;
            best = candidate;
        }
    }
    if (best != NULL)
    {
        const uint8_t mode = best->contact_axis_20 > actor->contact_axis_20 ? 9u : 8u;
        const uint8_t before = best->contact_mode_6e;
        apply_contact_reaction(best, mode, roster->roster_rules);
        if (best->contact_mode_6e != before)
            ++roster->contact_events;
    }
    return PORTABLE_GP_ROSTER_OK;
}

static void apply_view_contact_reaction(PortableGpRaceRoster *roster, size_t recipient,
                                        uint8_t mode,
                                        const uint8_t view_angles[PORTABLE_GP_RACER_COUNT])
{
    PortableGpRacer *racer = &roster->racers[recipient];
    /* D214 excludes class8; AA6C independently gates EACH recipient's CA78. */
    if (racer->elimination.revenge_bomb ||
        (view_angles[recipient] != 0u && view_angles[recipient] != 127u))
        return;
    const uint8_t before = racer->contact_mode_6e;
    apply_contact_reaction(racer, mode, roster->roster_rules);
    if (racer->contact_mode_6e != before)
        ++roster->contact_events;
}

PortableGpRosterStatus
portable_gp_race_roster_resolve_view_contacts(PortableGpRaceRoster *roster, size_t selected,
                                              size_t route_count,
                                              const uint8_t view_angles[PORTABLE_GP_RACER_COUNT])
{
    if (roster == NULL || route_count == 0u || view_angles == NULL ||
        selected >= portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *actor = &roster->racers[selected];
    /* C4C0 C74E skips the whole D00C pass for the revenge-bomb role. */
    if (actor->elimination.revenge_bomb)
        return PORTABLE_GP_ROSTER_OK;
    size_t best = SIZE_MAX;
    unsigned best_delta = 0x1000u;
    for (size_t index = 0u; index < portable_gp_race_roster_count(roster); ++index)
    {
        if (index == selected)
            continue;
        PortableGpRacer *candidate = &roster->racers[index];
        if (!routes_touch(actor->route_index, candidate->route_index, route_count))
            continue;
        const uint8_t bits =
            (uint8_t)((uint8_t)actor->contact_axis_1e - (uint8_t)candidate->contact_axis_1e);
        const int signed_delta = bits < 128u ? (int)bits : (int)bits - 256;
        const unsigned delta = (unsigned)(signed_delta < 0 ? -signed_delta : signed_delta);
        if (actor->contact_axis_1e < candidate->contact_axis_1e)
        {
            if (delta <= 55u)
                apply_view_contact_reaction(
                    roster, selected, actor->contact_axis_20 > candidate->contact_axis_20 ? 9u : 8u,
                    view_angles);
        }
        else if (delta < best_delta)
        {
            best_delta = delta;
            best = index;
        }
    }
    if (best != SIZE_MAX)
        apply_view_contact_reaction(
            roster, best, roster->racers[best].contact_axis_20 > actor->contact_axis_20 ? 9u : 8u,
            view_angles);
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_resolve_player_contact(PortableGpRaceRoster *roster,
                                                                      size_t route_count,
                                                                      uint16_t camera_yaw)
{
    if (roster == NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    uint8_t views[PORTABLE_GP_RACER_COUNT] = {0};
    for (size_t index = 0u; index < portable_gp_race_roster_count(roster); ++index)
    {
        const uint16_t angle =
            index == 0u ? roster->racers[index].heading : roster->racers[index].contact_heading_56;
        views[index] = (uint8_t)(((uint16_t)(camera_yaw - angle) >> 9u) & 127u);
    }
    return portable_gp_race_roster_resolve_view_contacts(roster, 0u, route_count, views);
}

PortableGpRosterStatus
portable_gp_race_roster_tick_contact_animation(PortableGpRacer *racer, uint8_t view_angle,
                                               const PortableDriverRosterRules *rules)
{
    /* _080F2C14 frame/duration pairs, consumed by sub_805AAF0/805ABB8.
     * Terminal mode10 seeds 63 from _080F2C22[1], then resumes the
     * F2C14 durations. It no longer selects special kart art. */

    if (racer == NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const unsigned mode = racer->contact_mode_6e & 15u;
    if (mode == 0u)
        return PORTABLE_GP_ROSTER_OK;
    if (rules == NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (mode != 8u && mode != 9u && mode != 10u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (mode != 10u &&
        ((view_angle != 0u && view_angle != 127u) || racer->contact_height_24 > 511u))
    {
        /* AAF0 -> AC24 cancels before decrementing. Clear phase bits
         * 4..8, seed _080F2C22[1]=63 and retain frame/flip flags. */
        racer->contact_mode_6e = 10u;
        racer->contact_phase_6f = (uint8_t)(rules->contact_script[7][1] << 1u);
        return PORTABLE_GP_ROSTER_OK;
    }
    const uint16_t packed = (uint16_t)racer->contact_mode_6e | (uint16_t)racer->contact_phase_6f
                                                                   << 8u;
    unsigned phase = (packed >> 4u) & 31u;
    const uint8_t remaining = (uint8_t)((packed >> 9u) - 1u);
    if (remaining != 0u)
    {
        racer->contact_phase_6f =
            (uint8_t)((racer->contact_phase_6f & 1u) | (uint8_t)(remaining << 1u));
        return PORTABLE_GP_ROSTER_OK;
    }
    ++phase;
    if (phase >= 7u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (mode != 10u)
        racer->contact_frame_6c = rules->contact_script[phase][0];
    if (rules->contact_script[phase][1] == 0u)
    {
        racer->contact_mode_6e = mode == 10u ? 0u : 10u;
        racer->contact_phase_6f = mode == 10u ? 0u : (uint8_t)(rules->contact_script[7][1] << 1u);
        return PORTABLE_GP_ROSTER_OK;
    }
    const uint16_t next = (uint16_t)((packed & UINT16_C(0xfe0f)) | (phase << 4u));
    racer->contact_mode_6e = (uint8_t)next;
    racer->contact_phase_6f =
        (uint8_t)(((next >> 8u) & 1u) | (rules->contact_script[phase][1] << 1u));
    return PORTABLE_GP_ROSTER_OK;
}

void portable_gp_race_roster_cache_contact_center(PortableGpRacer *racer)
{
    if (racer == NULL)
        return;
    /* C4C0 stores the pre-motion integer coordinates at +0xee/+0xf0.
     * The collision pass in the next frame consumes that cached pair. */
    racer->contact_center_x = (int16_t)(racer->position_x_q16 / INT32_C(0x10000));
    racer->contact_center_y = (int16_t)(racer->position_y_q16 / INT32_C(0x10000));
}

PortableGpRosterStatus portable_gp_race_roster_begin_unboosted_kart_contact(
    PortableGpRaceRoster *roster, size_t first_index, size_t second_index, uint32_t scene_clock,
    const int32_t *sine, size_t sine_count)
{
    if (roster == NULL || sine == NULL || sine_count < 1024u || first_index == 0u ||
        second_index == 0u || first_index >= portable_gp_race_roster_count(roster) ||
        second_index >= portable_gp_race_roster_count(roster) || first_index == second_index)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    unsigned ranked = 0u;
    for (size_t i = 0u; i < portable_gp_race_roster_count(roster); ++i)
    {
        const unsigned index = roster->standings[i];
        if (index >= 8u || (ranked & (1u << index)) != 0u)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        ranked |= 1u << index;
    }
    const PortableGpRacer *first = &roster->racers[first_index];
    const PortableGpRacer *second = &roster->racers[second_index];
    const size_t indices[2] = {first_index, second_index};
    for (size_t i = 0u; i < 2u; ++i)
    {
        const PortableGpRacer *r = &roster->racers[indices[i]];
        if (r->is_player || r->driver_class >= 8u || r->source_flags != UINT32_C(0x2000) ||
            r->banana_hit_timer_38 > 1u || r->contact_status_60 != 0u ||
            r->contact_status_66 != 0 || r->lightning_status_6a != 0 ||
            r->lightning_state_78 != 0u || r->world_height_q16 != 0 ||
            r->vertical_velocity_q8 != 0 || (r->hit_cue_flags_58 & UINT16_C(0x40)) != 0u ||
            (r->rank_flags_ba >> 1u) > 7u)
            return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
    }
    /* Truncate wrapped whole-world differences before ArcTan2. The first
     * impulse is perpendicular to the second target heading, on the
     * first racer's side; the second receives the opposite impulse. */
    const int32_t dx =
        signed32_bits((uint32_t)first->position_x_q16 - (uint32_t)second->position_x_q16) / 65536;
    const int32_t dy =
        signed32_bits((uint32_t)first->position_y_q16 - (uint32_t)second->position_y_q16) / 65536;
    uint16_t axis;
    if (portable_driver_vector_effect_source_angle(signed16((uint16_t)dx), signed16((uint16_t)dy),
                                                   &axis) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const uint16_t impulse =
        (uint16_t)(second->heading + (signed16((uint16_t)(second->heading - axis)) >= 0
                                          ? UINT16_C(0xc000)
                                          : UINT16_C(0x4000)));
    const KartContactTransaction transaction =
        begin_contact_transaction(roster, first_index, second_index);
    PortableGpRaceRoster *next = roster;
    for (size_t i = 0u; i < 2u; ++i)
    {
        PortableGpRacer *r = &next->racers[indices[i]];
        const size_t count = r->rank_flags_ba >> 1u;
        uint32_t ranked_flags[7];
        for (size_t rank = 0u; rank < count; ++rank)
            ranked_flags[rank] = next->racers[next->standings[rank]].source_flags;
        const PortableGpRosterStatus status = portable_gp_race_roster_begin_kart_hit(
            r, NULL, scene_clock, ranked_flags, count, sine, sine_count);
        if (status != PORTABLE_GP_ROSTER_OK)
            return reject_contact_transaction(&transaction, status);
        const uint16_t direction = (uint16_t)(impulse + (i == 0u ? UINT16_C(0) : UINT16_C(0x8000)));
        const uint32_t index = impulse_sine_index(direction);
        r->component_x = signed16((uint16_t)((int64_t)sine[index] * 512 / 32768));
        r->component_y =
            signed16((uint16_t)(-(int64_t)sine[(256u - index) & UINT32_C(0x3ff)] * 512 / 32768));
    }
    ++next->contact_events;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus
portable_gp_race_roster_begin_normal_kart_contact(PortableGpRaceRoster *roster, size_t first_index,
                                                  size_t second_index, const int32_t *sine,
                                                  size_t sine_count)
{
    if (roster == NULL || first_index >= portable_gp_race_roster_count(roster) ||
        second_index >= portable_gp_race_roster_count(roster) || first_index == second_index ||
        sine == NULL || sine_count < 1024u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const KartContactTransaction transaction =
        begin_contact_transaction(roster, first_index, second_index);
    PortableGpRaceRoster *next = roster;
    PortableGpRacer *a = &next->racers[first_index], *b = &next->racers[second_index];
    const uint16_t cues = a->hit_cue_flags_58 ^ b->hit_cue_flags_58;
    const int both_protected = (a->hit_cue_flags_58 & b->hit_cue_flags_58 & UINT16_C(0x8000)) != 0u;
    if (a->banana_hit_timer_38 > 1u || b->banana_hit_timer_38 > 1u || a->contact_status_60 != 0u ||
        b->contact_status_60 != 0u || a->contact_status_66 != 0 || b->contact_status_66 != 0 ||
        a->lightning_state_78 > 1u || b->lightning_state_78 > 1u ||
        a->world_height_q16 > INT32_C(0x4ffff) || b->world_height_q16 > INT32_C(0x4ffff) ||
        (!both_protected && (cues & UINT16_C(0x8040)) != 0u))
        return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
    if (apply_kart_contact_response(
            next, first_index, second_index,
            signed16((uint16_t)((uint16_t)a->contact_center_x - (uint16_t)b->contact_center_x)),
            signed16((uint16_t)((uint16_t)a->contact_center_y - (uint16_t)b->contact_center_y)),
            sine, sine_count) != PORTABLE_GP_ROSTER_OK)
        return reject_contact_transaction(&transaction, PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    a->source_flags |= UINT32_C(0x1000);
    b->source_flags |= UINT32_C(0x1000);
    a->banana_hit_timer_38 = b->banana_hit_timer_38 = 8u;
    a->kart_contact_state_5a = b->kart_contact_state_5a = UINT16_C(0x8007);
    /* F10C calls D4E4(actor,3) on each kart: only an unfinished local
     * driver loses one coin in GP; CPU inventories are untouched. */
    lose_racer_coins(a, 3u, 1u, next->coin_effect_clock);
    lose_racer_coins(b, 3u, 1u, next->coin_effect_clock);
    ++next->contact_events;
    return PORTABLE_GP_ROSTER_OK;
}

typedef struct
{
    PortableGpRaceRoster *roster;
    PortableGpKartContactReport *report;
    const int32_t *sine;
    size_t sine_count;
    uint32_t clock;
    PortableGpRosterStatus status;
    PortableCollisionActor *actors;
} KartContactDispatch;

static void dispatch_kart_contact(PortableCollisionActor *actor, PortableCollisionActor *other,
                                  void *user)
{
    KartContactDispatch *d = user;
    if (d->status != PORTABLE_GP_ROSTER_OK)
        return;
    const unsigned first = actor->tag, second = other->tag;
    PortableGpRacer *a = &d->roster->racers[first], *b = &d->roster->racers[second];
    /* F450 rechecks mutable guards on the reverse callback. */
    if ((a->banana_hit_timer_38 | b->banana_hit_timer_38) > 1u ||
        (a->contact_status_60 | b->contact_status_60) != 0u || a->contact_status_66 != 0 ||
        b->contact_status_66 != 0 || a->lightning_state_78 > 1u || b->lightning_state_78 > 1u ||
        a->world_height_q16 > 0x4ffff || b->world_height_q16 > 0x4ffff)
        return;
    const uint16_t prior_a = a->banana_hit_timer_38, prior_b = b->banana_hit_timer_38;
    const uint32_t prior_events = d->roster->contact_events;
    const unsigned protected_a = (a->hit_cue_flags_58 & 0x8000u) != 0u;
    const unsigned protected_b = (b->hit_cue_flags_58 & 0x8000u) != 0u;
    const unsigned boost_a = (a->hit_cue_flags_58 & 0x40u) != 0u;
    const unsigned boost_b = (b->hit_cue_flags_58 & 0x40u) != 0u;
    const unsigned normal_a = (a->hit_cue_flags_58 & 0x10u) != 0u;
    const unsigned normal_b = (b->hit_cue_flags_58 & 0x10u) != 0u;
    /* F450 queues selected-kart bump audio before E92C decides whether a
     * protected/size hit can affect the victim. Finished actors and Battle
     * revenge bombs bypass this audio path, independently of physics. */
    if (!a->elimination.revenge_bomb && !b->elimination.revenge_bomb)
    {
        const unsigned indices[2] = {first, second};
        for (unsigned i = 0u; i < 2u; ++i)
        {
            const unsigned index = indices[i];
            const PortableGpRacer *r = &d->roster->racers[index];
            if (r->is_player && !(r->source_flags & UINT32_C(0x20)))
            {
                d->report->bump[index] = 1u;
                d->report->bump_speed_q16[index] =
                    a->speed_q16 > b->speed_q16 ? a->speed_q16 : b->speed_q16;
            }
        }
    }
    if (protected_a != protected_b &&
        d->roster->racers[protected_a ? second : first].lightning_status_6a != 0)
        return;
    if (!protected_a && !protected_b && boost_a != boost_b &&
        d->roster->racers[boost_a ? second : first].lightning_status_6a != 0)
        return;
    uint8_t response = PORTABLE_GP_KART_CONTACT_PUSH;
    if (a->elimination.revenge_bomb || b->elimination.revenge_bomb)
    {
        uint8_t contacted = 0u;
        d->status = portable_battle_race_roster_bomb_contact(d->roster, first, second, d->clock,
                                                             d->sine, d->sine_count, &contacted);
        response = PORTABLE_GP_KART_CONTACT_HIT;
    }
    else if (protected_a || protected_b)
    {
        d->status = portable_gp_race_roster_resolve_protected_kart_contact(
            d->roster, first, second, d->clock, d->sine, d->sine_count);
        if (protected_a != protected_b)
            response = PORTABLE_GP_KART_CONTACT_HIT;
    }
    else if (boost_a != boost_b || normal_a != normal_b)
    {
        const unsigned size_response = boost_a == boost_b;
        const unsigned first_hits = size_response ? normal_a : boost_a;
        const unsigned victim = first_hits ? second : first, hitter = first_hits ? first : second;
        const unsigned count = d->roster->racers[victim].rank_flags_ba >> 1u;
        if (count > 7u)
        {
            d->status = PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
            return;
        }
        uint32_t ranked_flags[7];
        for (unsigned rank = 0u; rank < count; ++rank)
        {
            const unsigned index = d->roster->standings[rank];
            if (index >= portable_gp_race_roster_count(d->roster))
            {
                d->status = PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
                return;
            }
            ranked_flags[rank] = d->roster->racers[index].source_flags;
        }
        if (size_response)
            note_player_bump(d->roster, first, second);
        d->status = portable_gp_race_roster_begin_kart_hit(
            &d->roster->racers[victim], &d->roster->racers[hitter], d->clock, ranked_flags, count,
            d->sine, d->sine_count);
        if (d->status == PORTABLE_GP_ROSTER_OK)
        {
            if (size_response)
            {
                /* EFBE/F014 queues the same delayed victim voice as Star. */
                portable_gp_race_roster_schedule_voice(d->roster, (uint8_t)victim, 16u, 0u);
            }
            ++d->roster->contact_events;
        }
        response = PORTABLE_GP_KART_CONTACT_HIT;
    }
    else
        d->status = portable_gp_race_roster_begin_normal_kart_contact(d->roster, first, second,
                                                                      d->sine, d->sine_count);
    a = &d->roster->racers[first];
    b = &d->roster->racers[second];
    d->actors[first].filter_flags = a->banana_hit_timer_38;
    d->actors[second].filter_flags = b->banana_hit_timer_38;
    if (d->roster->contact_events == prior_events)
        return;
    ++a->kart_contact_events;
    ++b->kart_contact_events;
    const unsigned indices[2] = {first, second};
    const uint16_t prior[2] = {prior_a, prior_b};
    for (unsigned i = 0u; i < 2u; ++i)
    {
        const unsigned index = indices[i];
        const PortableGpRacer *r = &d->roster->racers[index];
        if (r->banana_hit_timer_38 > prior[i] && r->banana_hit_timer_38 > 1u)
            d->report->response[index] = response;
    }
}

PortableGpRosterStatus portable_gp_race_roster_collide_karts(PortableGpRaceRoster *roster,
                                                             uint16_t checkpoints,
                                                             uint32_t scene_clock,
                                                             const int32_t *sine, size_t sine_count,
                                                             PortableGpKartContactReport *report)
{
    if (!roster || !sine || sine_count < 1024u || !report || !checkpoints || checkpoints > 127u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    *report = (PortableGpKartContactReport){0};
    PortableCollisionActor actors[PORTABLE_GP_RACER_COUNT] = {0};
    PortableCollisionList list = {0};
    KartContactDispatch dispatch = {
        roster, report, sine, sine_count, scene_clock, PORTABLE_GP_ROSTER_OK, actors};
    roster->coin_effect_clock = scene_clock;
    for (size_t i = 0u; i < portable_gp_race_roster_count(roster); ++i)
    {
        const PortableGpRacer *r = &roster->racers[i];
        actors[i] = (PortableCollisionActor){.pos_x = r->position_x_q16,
                                             .pos_y = r->position_y_q16,
                                             .pos_z = r->world_height_q16,
                                             .size_x = 10u,
                                             .size_y = 10u,
                                             .size_z = 10u,
                                             .filter_flags = r->banana_hit_timer_38,
                                             .checkpoint_id = r->route_index,
                                             .tag = (uint8_t)i,
                                             .collide = dispatch_kart_contact,
                                             .user = &dispatch};
        if (portable_collision_add_active(&list, &actors[i]) != PORTABLE_COLLISION_OK)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    }
    if (portable_collision_collide(&list, checkpoints,
                                   ((roster->race_mode_flags_7da & 0x800u) || checkpoints < 5u)
                                       ? PORTABLE_COLLISION_FULL
                                       : PORTABLE_COLLISION_FAST) != PORTABLE_COLLISION_OK)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    return dispatch.status;
}

PortableGpRosterStatus portable_gp_race_roster_tick_stationary(PortableGpRaceRoster *roster)
{
    if (roster == NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    for (size_t index = 0u; index < portable_gp_race_roster_count(roster); ++index)
        roster->racers[index].last_update_serial = ++roster->update_serial;
    ++roster->authoritative_ticks;
    return PORTABLE_GP_ROSTER_OK;
}

static uint64_t hash_u32(uint64_t hash, uint32_t value)
{
    for (unsigned byte = 0u; byte < 4u; ++byte)
    {
        hash ^= (value >> (byte * 8u)) & UINT32_C(0xff);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t hash_green_shell(uint64_t hash, const PortableGpGreenShell *shell)
{
    hash = hash_u32(hash, (uint32_t)shell->position_x_q16);
    hash = hash_u32(hash, (uint32_t)shell->position_y_q16);
    hash = hash_u32(hash, (uint32_t)shell->position_z_q16);
    hash = hash_u32(hash, (uint32_t)shell->velocity_x_q16);
    hash = hash_u32(hash, (uint32_t)shell->velocity_y_q16);
    hash = hash_u32(hash, (uint32_t)shell->velocity_z_q16);
    hash = hash_u32(hash, shell->creation_order);
    hash = hash_u32(hash, shell->kind_10);
    hash = hash_u32(hash, shell->angle_26);
    hash = hash_u32(hash, shell->flags_34);
    hash = hash_u32(hash, shell->animation_tick_4c);
    hash = hash_u32(hash, shell->lifetime_38);
    hash = hash_u32(hash, shell->collision_state_50);
    hash = hash_u32(hash, shell->terrain_state_52);
    hash = hash_u32(hash, shell->floor_36);
    hash = hash_u32(hash, shell->animation_index_70);
    hash = hash_u32(hash, shell->display_index);
    hash = hash_u32(hash, shell->state_71);
    hash = hash_u32(hash, shell->collision_flags_4e);
    hash = hash_u32(hash, shell->release_timer_74);
    hash = hash_u32(hash, shell->phase);
    hash = hash_u32(hash, shell->occupied);
    hash = hash_u32(hash, (uint32_t)shell->vertical_velocity_q16);
    hash = hash_u32(hash, shell->destruction_field_5a);
    hash = hash_u32(hash, shell->collision_result);
    hash = hash_u32(hash, shell->contact_audio_pending);
    hash = hash_u32(hash, shell->launch_audio_pending);
    hash = hash_u32(hash, shell->orbit_audio_pending);
    hash = hash_u32(hash, shell->retire_held_slot);
    return hash;
}

static uint64_t hash_banana(uint64_t hash, const PortableGpBanana *banana)
{
    hash = hash_u32(hash, (uint32_t)banana->position_x_q16);
    hash = hash_u32(hash, (uint32_t)banana->position_y_q16);
    hash = hash_u32(hash, (uint32_t)banana->position_z_q16);
    hash = hash_u32(hash, banana->creation_order);
    hash = hash_u32(hash, banana->collision_guard_38);
    hash = hash_u32(hash, banana->motion_audio_pending);
    hash = hash_u32(hash, (uint32_t)banana->velocity_x_q16);
    hash = hash_u32(hash, (uint32_t)banana->velocity_y_q16);
    hash = hash_u32(hash, banana->material_4e);
    hash = hash_u32(hash, banana->collision_state_50);
    hash = hash_u32(hash, banana->kind_10);
    hash = hash_u32(hash, banana->angle_26);
    hash = hash_u32(hash, banana->flags_34);
    hash = hash_u32(hash, banana->attachment_ticks_4c);
    hash = hash_u32(hash, banana->floor_36);
    hash = hash_u32(hash, banana->phase);
    hash = hash_u32(hash, banana->occupied);
    hash = hash_u32(hash, banana->collision_active);
    hash = hash_u32(hash, (uint32_t)banana->vertical_velocity_q16);
    hash = hash_u32(hash, banana->destruction_field_5a);
    hash = hash_u32(hash, banana->collision_result);
    hash = hash_u32(hash, banana->contact_audio_pending);
    return hash;
}

static uint64_t hash_red_shell(uint64_t hash, const PortableGpRedShell *shell)
{
    hash = hash_u32(hash, (uint32_t)shell->position_x_q16);
    hash = hash_u32(hash, (uint32_t)shell->position_y_q16);
    hash = hash_u32(hash, (uint32_t)shell->position_z_q16);
    hash = hash_u32(hash, shell->creation_order);
    hash = hash_u32(hash, shell->kind_10);
    hash = hash_u32(hash, shell->angle_26);
    hash = hash_u32(hash, shell->flags_34);
    hash = hash_u32(hash, shell->animation_tick_4c);
    hash = hash_u32(hash, shell->material_4e);
    hash = hash_u32(hash, shell->collision_state_50);
    hash = hash_u32(hash, shell->terrain_cell_52);
    hash = hash_u32(hash, shell->trap_audio_pending);
    hash = hash_u32(hash, shell->lifetime_38);
    hash = hash_u32(hash, (uint32_t)shell->velocity_x_q16);
    hash = hash_u32(hash, (uint32_t)shell->velocity_y_q16);
    hash = hash_u32(hash, (uint32_t)shell->vertical_velocity_q16);
    hash = hash_u32(hash, shell->floor_36);
    hash = hash_u32(hash, shell->animation_index_70);
    hash = hash_u32(hash, shell->display_index);
    hash = hash_u32(hash, shell->phase);
    hash = hash_u32(hash, shell->occupied);
    hash = hash_u32(hash, shell->release_timer_74);
    hash = hash_u32(hash, shell->speed_byte_71);
    hash = hash_u32(hash, shell->route_budget_78);
    hash = hash_u32(hash, shell->previous_floor_79);
    hash = hash_u32(hash, shell->steering_delay_7a);
    hash = hash_u32(hash, shell->target_racer_index);
    hash = hash_u32(hash, shell->target_valid);
    hash = hash_u32(hash, shell->spiny_targeting);
    hash = hash_u32(hash, shell->motion_audio_pending);
    hash = hash_u32(hash, shell->flight_audio_pending);
    hash = hash_u32(hash, (uint32_t)shell->flight_audio_x_q16);
    hash = hash_u32(hash, (uint32_t)shell->flight_audio_y_q16);
    hash = hash_u32(hash, shell->flight_audio_heading);
    hash = hash_u32(hash, shell->flight_audio_speed);
    hash = hash_u32(hash, shell->orbit_audio_pending);
    hash = hash_u32(hash, shell->retire_held_slot);
    hash = hash_u32(hash, shell->destruction_field_5a);
    hash = hash_u32(hash, shell->collision_active);
    hash = hash_u32(hash, shell->collision_result);
    hash = hash_u32(hash, shell->contact_audio_pending);
    return hash;
}

uint64_t portable_gp_race_roster_identity(const PortableGpRaceRoster *roster)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    if (roster == NULL)
        return 0u;
    for (unsigned i = 0u; i < PORTABLE_GP_ITEM_POOL_CAPACITY; ++i)
    {
        hash = hash_u32(hash, roster->green_pool[i].owner);
        hash = hash_green_shell(hash, &roster->green_pool[i].shell);
    }
    for (unsigned i = 0u; i < PORTABLE_GP_ITEM_POOL_CAPACITY; ++i)
    {
        hash = hash_u32(hash, roster->banana_pool[i].owner);
        hash = hash_banana(hash, &roster->banana_pool[i].banana);
        hash = hash_u32(hash, roster->red_pool[i].owner);
        hash = hash_red_shell(hash, &roster->red_pool[i].shell);
    }
    for (unsigned kind = 0u; kind < 2u; ++kind)
        for (unsigned i = 0u; i < 3u; ++i)
            hash = hash_u32(hash, roster->shell_orbit_delay[kind][i]);
    hash = hash_u32(hash, roster->shell_orbit_delay_valid);
    if (roster->racer_count != 0u)
        hash = hash_u32(hash, roster->racer_count);
    hash = hash_u32(hash, roster->course_id);
    hash = hash_u32(hash, roster->authoritative_ticks);
    hash = hash_u32(hash, roster->update_serial);
    hash = hash_u32(hash, roster->ai_mode_row_c64);
    for (size_t index = 0u; index < portable_gp_race_roster_count(roster); ++index)
    {
        const PortableGpRacer *racer = &roster->racers[index];
        hash = hash_u32(hash, (uint32_t)racer->position_x_q16);
        hash = hash_u32(hash, (uint32_t)racer->position_y_q16);
        hash = hash_u32(hash, racer->source_flags);
        hash = hash_u32(hash, racer->grid_slot);
        hash = hash_u32(hash, racer->driver_class);
        hash = hash_u32(hash, racer->is_player);
        hash = hash_u32(hash, racer->route_index);
        hash = hash_u32(hash, racer->decision_valid);
        hash = hash_u32(hash, racer->control);
        hash = hash_u32(hash, racer->route_angle);
        hash = hash_u32(hash, (uint32_t)racer->acceleration_q16);
        hash = hash_u32(hash, (uint32_t)racer->speed_q16);
        hash = hash_u32(hash, racer->heading);
        hash = hash_u32(hash, racer->source_heading);
        hash = hash_u32(hash, (uint16_t)racer->release_angle_offset);
        hash = hash_u32(hash, (uint16_t)racer->component_x);
        hash = hash_u32(hash, (uint16_t)racer->component_y);
        hash = hash_u32(hash, (uint16_t)racer->progress_ae);
        hash = hash_u32(hash, racer->route_wrap_91);
        hash = hash_u32(hash, racer->route_row_191);
        hash = hash_u32(hash, racer->rank_row_9a);
        hash = hash_u32(hash, racer->rank_flags_ba);
        hash = hash_u32(hash, racer->acceleration_mode_19b);
        hash = hash_u32(hash, racer->speed_target_bias_19d);
        hash = hash_u32(hash, racer->coin_count_178);
        hash = hash_u32(hash, racer->kart_contact_events);
        hash = hash_u32(hash, (uint16_t)racer->contact_axis_1e);
        hash = hash_u32(hash, (uint16_t)racer->contact_axis_20);
        hash = hash_u32(hash, racer->contact_height_24);
        hash = hash_u32(hash, racer->contact_heading_56);
        hash = hash_u32(hash, racer->contact_flags_6d);
        hash = hash_u32(hash, racer->contact_mode_6e);
        hash = hash_u32(hash, racer->contact_phase_6f);
        if ((roster->race_mode_flags_7da & UINT16_C(0x800)) != 0u)
        {
            hash = hash_u32(hash, racer->battle_navigation_valid);
            if (racer->battle_navigation_valid)
            {
                hash = hash_u32(hash, (uint16_t)racer->battle_waypoint_x);
                hash = hash_u32(hash, (uint16_t)racer->battle_waypoint_y);
                hash = hash_u32(hash, racer->battle_speed_target);
            }
            hash = hash_u32(hash, racer->elimination.remaining_ticks);
            hash = hash_u32(hash, racer->elimination.revenge_bomb);
            hash = hash_u32(hash, (uint16_t)racer->arena_start_x);
            hash = hash_u32(hash, (uint16_t)racer->arena_start_y);
            hash = hash_u32(hash, racer->arena_start_heading);
            hash = hash_u32(hash, racer->bomb_respawn_pending);
        }
        hash = hash_u32(hash, racer->item_inventory.held_item_17a);
        hash = hash_u32(hash, racer->item_inventory.preview_item);
        hash = hash_u32(hash, racer->item_inventory.settled_item);
        hash = hash_u32(hash, (uint32_t)racer->item_inventory.roulette_index_70);
        hash = hash_u32(hash, (uint32_t)racer->item_inventory.delay_74);
        hash = hash_u32(hash, (uint32_t)racer->item_inventory.timeline_index_78);
        hash = hash_u32(hash, racer->item_inventory.player_owned);
        hash = hash_u32(hash, racer->item_inventory.active);
        hash = hash_green_shell(hash, &racer->green_shell);
        hash = hash_red_shell(hash, &racer->red_shell);
        hash = hash_banana(hash, &racer->banana);
        hash = hash_u32(hash, racer->item_decision_190);
        hash = hash_u32(hash, racer->item_decision_timer_18e);
        hash = hash_u32(hash, racer->shell_item_18c);
        hash = hash_u32(hash, racer->item_use_state_17c);
        hash = hash_u32(hash, racer->mushroom_response_flags_b4);
        hash = hash_u32(hash, racer->hit_response_b6);
        hash = hash_u32(hash, racer->effect_state_72);
        hash = hash_u32(hash, racer->hit_state_flags_74);
        hash = hash_u32(hash, racer->hit_cue_flags_58);
        hash = hash_u32(hash, racer->hit_ground_flags_96);
        hash = hash_u32(hash, racer->item_response_flags_b2);
        hash = hash_u32(hash, racer->item_lock_ac);
        hash = hash_u32(hash, (uint16_t)racer->contact_status_66);
        hash = hash_u32(hash, racer->lightning_timer_68);
        hash = hash_u32(hash, (uint16_t)racer->lightning_status_6a);
        hash = hash_u32(hash, racer->hit_angle_80);
        hash = hash_u32(hash, (uint16_t)racer->hit_spin_82);
        hash = hash_u32(hash, racer->hit_counter_88);
        hash = hash_u32(hash, racer->hit_state_7e);
        hash = hash_u32(hash, racer->lightning_state_78);
        hash = hash_u32(hash, racer->source_surface_selector_86);
        hash = hash_u32(hash, (uint32_t)racer->world_height_q16);
        hash = hash_u32(hash, (uint16_t)racer->vertical_velocity_q8);
        hash = hash_u32(hash, racer->recovery_material);
        hash = hash_u32(hash, racer->recovery_timer_9c);
        hash = hash_u32(hash, racer->recovery_contact_34);
        hash = hash_u32(hash, racer->fall_frames_76);
        hash = hash_u32(hash, (uint16_t)racer->recovery_target_x_9e);
        hash = hash_u32(hash, (uint16_t)racer->recovery_target_y_a0);
        hash = hash_u32(hash, racer->recovery_target_heading_a2);
        hash = hash_u32(hash, racer->red_shell_hit_audio_pending);
        hash = hash_u32(hash, racer->banana_hit_timer_38);
        hash = hash_u32(hash, racer->course_contact_hold);
        hash = hash_u32(hash, racer->flattened_64);
        hash = hash_u32(hash, racer->palette_requests_194);
        hash = hash_u32(hash, racer->burn_ticks_72);
        hash = hash_u32(hash, racer->affine.inverse_x_q8);
        hash = hash_u32(hash, racer->affine.inverse_y_q8);
        hash = hash_u32(hash, racer->affine.mode);
        hash = hash_u32(hash, racer->affine.row);
        hash = hash_u32(hash, racer->affine.remaining);
        hash = hash_u32(hash, racer->affine.pending_mode);
        hash = hash_u32(hash, racer->art_motion.countdown);
        hash = hash_u32(hash, racer->art_motion.wheel_phase);
        hash = hash_u32(hash, racer->art_motion.bob_phase);
        hash = hash_u32(hash, racer->art_motion.wheel_palette_bank);
        hash = hash_u32(hash, racer->art_motion.wheel_palette_valid);
        hash = hash_u32(hash, racer->art_motion.initialized);
        hash = hash_u32(hash, (uint32_t)racer->art_motion.cached_bob_offset);
        hash = hash_u32(hash, racer->palette_bank);
        hash = hash_u32(hash, racer->banana_spin_timer_c8);
        hash = hash_u32(hash, racer->banana_ground_timer_b8);
        hash = hash_u32(hash, racer->banana_reaction_volume);
        hash = hash_u32(hash, (uint16_t)racer->banana_reaction_pitch);
        hash = hash_u32(hash, racer->banana_reaction_audio_pending);
        hash = hash_u32(hash, racer->banana_special_entry_audio_pending);
        hash = hash_u32(hash, racer->banana_special_class_song);
        hash = hash_u32(hash, racer->banana_special_class_volume);
        hash = hash_u32(hash, racer->banana_special_class_player);
        hash = hash_u32(hash, racer->banana_special_class_audio_pending);
        hash = hash_u32(hash, racer->mushroom_timer_cc);
        hash = hash_u32(hash, racer->mushroom_mode_84);
        hash = hash_u32(hash, racer->golden_mushroom_ticks);
        hash = hash_u32(hash, racer->balloons_remaining);
        hash = hash_u32(hash, racer->motion_active);
        hash = hash_u32(hash, racer->last_update_serial);
    }
    for (unsigned i = 0u; i < PORTABLE_GP_ITEM_POOL_CAPACITY; ++i)
    {
        const PortableGpMushroomActor *actor = &roster->mushroom_pool[i];
        hash = hash_u32(hash, actor->creation_order);
        hash = hash_u32(hash, actor->ticks_remaining);
        hash = hash_u32(hash, actor->flags_34);
        hash = hash_u32(hash, actor->owner);
        hash = hash_u32(hash, actor->golden);
        hash = hash_u32(hash, actor->occupied);
    }
    hash = hash_u32(hash, roster->rank_cursor);
    for (size_t rank = 0u; rank < PORTABLE_GP_RACER_COUNT; ++rank)
        hash = hash_u32(hash, roster->standings[rank]);
    if ((roster->race_mode_flags_7da & UINT16_C(0x800)) != 0u)
        hash = hash_u32(hash, roster->battle_scene_state);
    hash = hash_u32(hash, roster->race_mode_flags_7da);
    hash = hash_u32(hash, roster->race_phase_7d4);
    hash = hash_u32(hash, roster->finish_countdown);
    hash = hash_u32(hash, roster->finish_result_delay_be4);
    hash = hash_u32(hash, roster->finish_player_rank);
    hash = hash_u32(hash, roster->finish_overlay_variant);
    hash = hash_u32(hash, roster->finish_banner_occupied);
    hash = hash_u32(hash, roster->finish_banner_visible);
    hash = hash_u32(hash, roster->finish_banner_stage);
    hash = hash_u32(hash, roster->finish_banner_y);
    hash = hash_u32(hash, roster->finish_banner_affine_scale);
    hash = hash_u32(hash, roster->finish_banner_hold_counter);
    hash = hash_u32(hash, roster->finish_banner_value);
    hash = hash_u32(hash, roster->contact_events);
    hash = hash_u32(hash, roster->coin_events);
    hash = hash_u32(hash, roster->item_box_events);
    hash = hash_u32(hash, roster->item_settlements);
    hash = hash_u32(hash, roster->item_hud.current);
    hash = hash_u32(hash, roster->item_hud.previous);
    hash = hash_u32(hash, roster->item_hud.remaining);
    hash = hash_u32(hash, roster->item_hud.displayed_item);
    hash = hash_u32(hash, roster->item_uses);
    hash = hash_u32(hash, roster->player_reserved_uses);
    hash = hash_u32(hash, roster->cpu_reserved_uses);
    hash = hash_u32(hash, roster->triple_green_uses);
    hash = hash_u32(hash, roster->triple_red_uses);
    hash = hash_u32(hash, roster->player_mushroom_uses);
    hash = hash_u32(hash, roster->cpu_mushroom_uses);
    hash = hash_u32(hash, roster->cpu_star_uses);
    hash = hash_u32(hash, roster->player_star_uses);
    hash = hash_u32(hash, roster->cpu_red_uses);
    hash = hash_u32(hash, roster->cpu_spiny_uses);
    hash = hash_u32(hash, roster->cpu_red_release_valid);
    for (unsigned side = 0u; side < 2u; ++side)
        for (unsigned gap = 0u; gap < 4u; ++gap)
            hash = hash_u32(hash, roster->cpu_red_release[side][gap]);
    hash = hash_u32(hash, roster->cpu_green_uses);
    hash = hash_u32(hash, roster->cpu_green_release_valid);
    for (unsigned side = 0u; side < 2u; ++side)
        for (unsigned gap = 0u; gap < 4u; ++gap)
            hash = hash_u32(hash, roster->cpu_green_release[side][gap]);
    hash = hash_u32(hash, roster->cpu_banana_uses);
    hash = hash_u32(hash, roster->cpu_banana_release_valid);
    for (unsigned side = 0u; side < 2u; ++side)
        for (unsigned gap = 0u; gap < 4u; ++gap)
            hash = hash_u32(hash, roster->cpu_banana_release[side][gap]);
    hash = hash_u32(hash, roster->cpu_item_clock);
    hash = hash_u32(hash, roster->cpu_item_cooldown_c62);
    hash = hash_u32(hash, roster->cpu_item_engine);
    hash = hash_u32(hash, roster->cpu_items_enabled);
    if ((roster->race_mode_flags_7da & 0x800u) != 0u)
    {
        hash = hash_u32(hash, roster->battle_combat.range);
        hash = hash_u32(hash, roster->battle_combat.aim_angle);
        hash = hash_u32(hash, roster->battle_combat.interval);
    }
    hash = hash_u32(hash, roster->red_shell_hits);
    hash = hash_u32(hash, roster->banana_hits);
    hash = hash_u32(hash, roster->lightning_duration_c82);
    hash = hash_u32(hash, roster->lightning_state_80d);
    hash = hash_u32(hash, roster->lightning_audio_pending);
    hash = hash_u32(hash, roster->lightning_events);
    hash = hash_u32(hash, roster->lightning_effect.script_tick);
    hash = hash_u32(hash, roster->lightning_effect.target_index);
    hash = hash_u32(hash, roster->lightning_effect.frame_index);
    hash = hash_u32(hash, roster->lightning_effect.occupied);
    hash = hash_u32(hash, roster->lightning_effect.visible);
    hash = hash_u32(hash, roster->lightning_effect_events);
    hash = hash_u32(hash, roster->boo_uses);
    hash = hash_u32(hash, roster->boo_thefts);
    for (unsigned i = 0u; i < PORTABLE_BOO_ATTACK_CAPACITY; ++i)
    {
        const PortableBooAttack *a = &roster->boo_attacks[i];
        hash = hash_u32(hash, (uint32_t)a->x_q16);
        hash = hash_u32(hash, (uint32_t)a->y_q16);
        hash = hash_u32(hash, (uint32_t)a->z_q16);
        hash = hash_u32(hash, (uint32_t)a->vx_q16);
        hash = hash_u32(hash, (uint32_t)a->vy_q16);
        hash = hash_u32(hash, (uint32_t)a->vz_q16);
        hash = hash_u32(hash, a->angle);
        hash = hash_u32(hash, a->bob_angle);
        hash = hash_u32(hash, a->timer);
        hash = hash_u32(hash, a->radius);
        hash = hash_u32(hash, (uint16_t)a->affine_y_q8);
        hash = hash_u32(hash, a->phase);
        hash = hash_u32(hash, a->target);
        hash = hash_u32(hash, a->occupied);
        hash = hash_u32(hash, a->visible);
        hash = hash_u32(hash, a->frame);
    }
    return hash;
}

const char *portable_gp_roster_status_name(PortableGpRosterStatus status)
{
    static const char *const names[] = {"ok", "invalid_argument", "unsupported_course",
                                        "route_lookup_failed", "item_ineligible"};
    return (unsigned)status < sizeof names / sizeof names[0] ? names[status] : "invalid_status";
}
