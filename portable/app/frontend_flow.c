#include "frontend_flow.h"

#include <string.h>
#include "../core/source_random.h"

static const uint8_t character_neighbors[PORTABLE_FRONTEND_DRIVER_SLOTS][4] = {
    {6u, 3u, 2u, 1u}, {7u, 4u, 0u, 2u}, {8u, 5u, 1u, 0u}, {0u, 6u, 5u, 4u}, {1u, 7u, 3u, 5u},
    {2u, 8u, 4u, 3u}, {3u, 0u, 8u, 7u}, {4u, 1u, 6u, 8u}, {5u, 2u, 7u, 6u},
};

static int valid_buttons(uint16_t buttons)
{
    return (buttons & (uint16_t)~UINT16_C(0x03ff)) == 0u;
}

static int emit_event(PortableFrontendFlow *flow, PortableFrontendEventType type, uint32_t value)
{
    if (flow->event_count >= PORTABLE_FRONTEND_EVENT_CAPACITY)
    {
        flow->event_overflow = 1u;
        return 0;
    }
    if (flow->page == PORTABLE_FRONTEND_PAGE_TITLE_MENU && type == PORTABLE_FRONTEND_EVENT_MOVE &&
        !flow->player_count_open)
        flow->title_cursor_motion.phase = 1u;
    if (flow->page == PORTABLE_FRONTEND_PAGE_CHARACTER)
    {
        if (type == PORTABLE_FRONTEND_EVENT_MOVE || type == PORTABLE_FRONTEND_EVENT_PAGE_ENTER)
        {
            flow->character_selection_tick = flow->tick;
            flow->character_animation_timing = 1u;
        }
        else if (type == PORTABLE_FRONTEND_EVENT_CHARACTER_LOCK)
        {
            flow->character_lock_tick = flow->tick;
            flow->character_animation_timing |= 2u;
        }
    }
    flow->events[flow->event_count++] =
        (PortableFrontendEvent){flow->tick, value, (uint8_t)type, (uint8_t)flow->page};
    return 1;
}

void portable_frontend_flow_consume_events(PortableFrontendFlow *flow)
{
    if (flow != NULL)
        flow->event_count = 0u;
}

static uint8_t custom_cup_access(const PortableFrontendFlow *flow)
{
    uint8_t tracks[PORTABLE_FRONTEND_CUPS] = {0}, mask = 0u;
    for (size_t index = 0u; index < flow->config.course_count; ++index)
    {
        const PortableFrontendCourseSlot *slot = &flow->config.courses[index];
        if (portable_track_set_of_cup(slot->cup) == PORTABLE_TRACK_SET_CUSTOM && slot->available)
            tracks[slot->cup - PORTABLE_FRONTEND_CUSTOM_CUP] |= (uint8_t)(1u << slot->track);
    }
    for (uint8_t cup = 0u; cup < PORTABLE_FRONTEND_CUPS; ++cup)
        if (flow->mode_cursor == 0u ? tracks[cup] == 15u : tracks[cup] != 0u)
            mask = (uint8_t)(mask | (1u << cup));
    return mask;
}

uint8_t portable_frontend_flow_cup_access(const PortableFrontendFlow *flow)
{
    if (flow == NULL || flow->race_speed >= 3u)
        return 0u;
    if (flow->mode_cursor == 3u)
        return 1u;
    /* 800B80A/800873C: Time Trial reads the 150cc lock column. */
    const uint8_t speed = flow->mode_cursor == 1u ? 2u : flow->race_speed;
    const PortableTrackSet set = portable_track_set_of_cup(flow->course_cup);
    if (set == PORTABLE_TRACK_SET_CUSTOM)
        return custom_cup_access(flow);
    if (set != PORTABLE_TRACK_SET_GBA)
        return flow->extra_cup_access[speed];
    const uint8_t mask = flow->cup_access[speed];
    return mask != 0u ? mask : UINT8_C(0x1f);
}

static void records_open(PortableFrontendFlow *flow);
static void clamp_cup(PortableFrontendFlow *flow)
{
    if (flow->mode_cursor == 3u)
    {
        flow->course_cup = PORTABLE_FRONTEND_ARENA_CUP;
        return;
    }
    PortableTrackSet set = portable_track_set_of_cup(flow->course_cup);
    if (set == PORTABLE_TRACK_SET_ARENA || set == PORTABLE_TRACK_SET_COUNT)
        flow->course_cup = 0u;
    uint8_t allowed = portable_frontend_flow_cup_access(flow);
    if (allowed == 0u)
    {
        flow->course_cup = 0u;
        allowed = portable_frontend_flow_cup_access(flow);
    }
    set = portable_track_set_of_cup(flow->course_cup);
    const uint8_t offset = portable_track_set_first_cup(set);
    if (!(allowed & (1u << (flow->course_cup - offset))))
        for (uint8_t cup = 0u; cup < PORTABLE_FRONTEND_CUPS; ++cup)
            if (allowed & (1u << cup))
            {
                flow->course_cup = (uint8_t)(offset + cup);
                flow->course_track = 0u;
                break;
            }
}

int portable_frontend_flow_set_extra_cup_access(PortableFrontendFlow *flow, const uint8_t masks[3])
{
    if (flow == NULL || masks == NULL)
        return 0;
    for (unsigned index = 0u; index < 3u; ++index)
        if ((masks[index] & UINT8_C(0xe0)) != 0u)
            return 0;
    memcpy(flow->extra_cup_access, masks, sizeof flow->extra_cup_access);
    clamp_cup(flow);
    return 1;
}

int portable_frontend_flow_set_cup_access(PortableFrontendFlow *flow, const uint8_t masks[3])
{
    if (flow == NULL || masks == NULL)
        return 0;
    for (unsigned index = 0u; index < 3u; ++index)
        if (masks[index] == 0u || (masks[index] & UINT8_C(0xe0)) != 0u)
            return 0;
    memcpy(flow->cup_access, masks, sizeof flow->cup_access);
    clamp_cup(flow);
    return 1;
}

static int mesh_first_cup(const PortableFrontendFlow *flow, uint8_t group, size_t start, int step,
                          uint8_t *out);
static int enter_page(PortableFrontendFlow *flow, PortableFrontendPage page)
{
    flow->page = page;
    flow->pending_page = 0;
    flow->page_ticks = 0u;
    flow->transition_ticks = 0u;
    flow->transition_total_ticks = 0u;
    flow->paused = page == PORTABLE_FRONTEND_PAGE_PAUSE ? 1u : 0u;
    if (page == PORTABLE_FRONTEND_PAGE_PAUSE)
        portable_pause_menu_init(&flow->pause_menu, flow->buttons);
    if (page == PORTABLE_FRONTEND_PAGE_CHARACTER)
    {
        flow->character_locked = 0u;
        flow->character_player = 0u;
        flow->platform = 0u;
        memset(flow->player_characters, 0, sizeof flow->player_characters);
    }
    if (page == PORTABLE_FRONTEND_PAGE_TITLE_INTRO || page == PORTABLE_FRONTEND_PAGE_TITLE_MENU)
    {
        flow->players = 1u;
        flow->player_count_open = 0u;
        flow->title_ship_ticks = flow->title_ambient_rng = 0u;
        flow->title_star_counter = flow->title_star_wait = 0u;
        flow->title_star_state = 0u;
        flow->title_cursor_motion.y = (int16_t)(132 - 16 * (int)flow->title_cursor);
        flow->title_cursor_motion.phase = flow->title_cursor_motion.ticks = 0u;
    }
    if (page == PORTABLE_FRONTEND_PAGE_COURSE || page == PORTABLE_FRONTEND_PAGE_RECORDS)
    {
        flow->course_phase = flow->mode_cursor == 3u ? 1u : 0u;
        flow->cup_motion_phase = flow->cup_motion_tick = 0u;
        flow->gp_preview_track = flow->gp_preview_ticks = 0u;
        flow->record_ghost_slot = 0u;
        portable_frontend_flow_set_ghost_choices(flow, 0u);
        clamp_cup(flow);
        /* A remembered 3D group stays while it offers the selected mode. */
        flow->mesh_request = 0u;
        if (flow->mesh_group != 0u)
        {
            uint8_t cup = flow->mesh_cup;
            if (page == PORTABLE_FRONTEND_PAGE_RECORDS ||
                (!portable_frontend_flow_mesh_cup_open(flow, cup) &&
                 !mesh_first_cup(flow, flow->mesh_group, 0u, 1, &cup)))
                flow->mesh_group = 0u;
            else
            {
                flow->mesh_cup = cup;
                flow->course_phase = 0u;
                flow->course_track = 0u;
            }
        }
        /* A locked platform opens on its own group; the base game never
         * opens on another platform's. */
        const uint8_t own = portable_frontend_flow_platform_group(flow, flow->platform);
        if (page == PORTABLE_FRONTEND_PAGE_COURSE && own != 0u && flow->mesh_group != own)
        {
            uint8_t cup = 0u;
            if (!mesh_first_cup(flow, own, 0u, 1, &cup))
                while (cup + 1u < flow->mesh_cup_count && flow->mesh_cups[cup].group + 1u != own)
                    ++cup;
            flow->mesh_group = own;
            flow->mesh_cup = cup;
            flow->course_phase = 0u;
            flow->course_track = 0u;
        }
        else if (flow->mesh_group != 0u && !portable_frontend_flow_group_allowed(flow, flow->mesh_group))
        {
            flow->mesh_group = 0u;
            flow->course_phase = flow->mode_cursor == 3u ? 1u : 0u;
        }
        /* Records is one list page: its board shows the highlighted course. */
        if (page == PORTABLE_FRONTEND_PAGE_RECORDS)
        {
            flow->course_phase = 2u;
            records_open(flow);
        }
    }
    return emit_event(flow, PORTABLE_FRONTEND_EVENT_PAGE_ENTER, (uint32_t)page);
}

/* CD00/DE8A: prefer the local ghost; otherwise choose a matching received
 * slot. The native presentation can expose both received slots independently. */
int portable_frontend_flow_set_karts(PortableFrontendFlow *flow, const PortableFrontendKartSlot *karts,
                                     size_t count)
{
    if (flow == NULL || (count && karts == NULL) || count > PORTABLE_FRONTEND_KART_CAPACITY)
        return 0;
    memset(flow->karts, 0, sizeof flow->karts);
    if (count)
        memcpy(flow->karts, karts, count * sizeof *karts);
    flow->kart_count = (uint8_t)count;
    flow->kart_cursor = 0u;
    flow->selected_kart = 0u;
    return 1;
}

static int karts_open(const PortableFrontendFlow *flow)
{
    return flow->kart_choice && flow->kart_count && flow->players <= 1u;
}

/* A newly locked driver starts on its own kart. */
static void lock_kart(PortableFrontendFlow *flow)
{
    flow->kart_cursor = 0u;
    flow->selected_kart = 0u;
    if (!karts_open(flow))
        return;
    for (uint8_t index = 0; index < flow->kart_count; ++index)
        if (flow->karts[index].owner == flow->selected_character && flow->karts[index].owner_default)
            flow->kart_cursor = index;
    flow->selected_kart = flow->karts[flow->kart_cursor].content_id;
}

int portable_frontend_flow_set_ghost_choices(PortableFrontendFlow *flow, uint8_t mask)
{
    if (flow == NULL || mask > 7u)
        return 0;
    if (flow->ghost_choice > 2u)
        flow->ghost_choice = 0u;
    flow->ghost_choices = mask;
    if (mask == 0u)
        flow->ghost_choice = 0u;
    else if ((mask & (1u << flow->ghost_choice)) == 0u)
    {
        flow->ghost_choice = 0u;
        while ((mask & (1u << flow->ghost_choice)) == 0u)
            ++flow->ghost_choice;
    }
    return 1;
}

static int begin_transition(PortableFrontendFlow *flow, PortableFrontendPage page,
                            PortableFrontendEventType event)
{
    if (!emit_event(flow, event, (uint32_t)page))
        return 0;
    /* Source controller phases include their terminal state turn. The outgoing
     * scene finishes before its successor enters; backgrounds never page-pan. */
    uint16_t outgoing = 17u, incoming = 18u;
    if (flow->page == PORTABLE_FRONTEND_PAGE_TITLE_INTRO)
        outgoing = 11u;
    else if (flow->page == PORTABLE_FRONTEND_PAGE_TITLE_MENU)
        outgoing = 18u;
    else if (flow->page == PORTABLE_FRONTEND_PAGE_MODE ||
             flow->page == PORTABLE_FRONTEND_PAGE_TIME_TRIAL)
        outgoing = page == PORTABLE_FRONTEND_PAGE_CHARACTER ? 66u
                   : (flow->page == PORTABLE_FRONTEND_PAGE_TIME_TRIAL ||
                      (page == PORTABLE_FRONTEND_PAGE_MODE && flow->mode_phase != 0u))
                       ? 17u
                       : 33u;
    else if (flow->page == PORTABLE_FRONTEND_PAGE_CHARACTER)
        outgoing = page == PORTABLE_FRONTEND_PAGE_COURSE ? 99u : 50u;
    else if (flow->page == PORTABLE_FRONTEND_PAGE_COURSE)
        outgoing = page == PORTABLE_FRONTEND_PAGE_CHARACTER ? 1u : 50u;
    if (page == PORTABLE_FRONTEND_PAGE_MODE)
        incoming = flow->page == PORTABLE_FRONTEND_PAGE_TITLE_MENU
                       ? (flow->players > 1u ? 110u : 114u)
                   : flow->page == PORTABLE_FRONTEND_PAGE_CHARACTER ? 47u
                                                                    : 32u;
    else if (page == PORTABLE_FRONTEND_PAGE_TIME_TRIAL)
        incoming = flow->page == PORTABLE_FRONTEND_PAGE_CHARACTER ? 43u : 28u;
    else if (page == PORTABLE_FRONTEND_PAGE_TITLE_MENU)
        incoming = 17u;
    else if (page == PORTABLE_FRONTEND_PAGE_TITLE_INTRO)
        incoming = 32u;
    else if (page == PORTABLE_FRONTEND_PAGE_RECORDS)
        incoming = 17u;
    if (page == PORTABLE_FRONTEND_PAGE_RACE)
    {
        outgoing = PORTABLE_FRONTEND_COURSE_RACE_TICKS;
        incoming = 0u;
    }
    if (flow->page >= PORTABLE_FRONTEND_PAGE_RACE)
    {
        outgoing = PORTABLE_FRONTEND_SHORT_TRANSITION_TICKS;
        incoming = 0u;
    }
    flow->transition_from_mode_phase = flow->mode_phase;
    flow->pending_page = page;
    flow->transition_out_ticks = flow->short_transitions ? 0u : outgoing;
    flow->transition_ticks = flow->short_transitions ? 1u : (uint16_t)(outgoing + incoming);
    flow->transition_total_ticks = flow->transition_ticks;
    return 1;
}

static int move_clamped(PortableFrontendFlow *flow, uint8_t *cursor, uint8_t limit, int direction)
{
    const uint8_t before = *cursor;
    if (direction < 0)
    {
        if (*cursor != 0u)
            --*cursor;
    }
    else if (*cursor + 1u < limit)
        ++*cursor;
    return before == *cursor || emit_event(flow, PORTABLE_FRONTEND_EVENT_MOVE, *cursor);
}

/* 42C4 medallion rows: Up/Left step back and Down/Right forward, wrapping. */
static int move_wrapped(PortableFrontendFlow *flow, uint8_t *cursor, uint8_t limit, int direction)
{
    if (direction < 0)
        return 1;
    *cursor = (uint8_t)((*cursor + (direction == 0 || direction == 2 ? limit - 1u : 1u)) % limit);
    return emit_event(flow, PORTABLE_FRONTEND_EVENT_MOVE, *cursor);
}

static int direction_index(uint16_t repeated)
{
    const uint16_t directions = repeated & UINT16_C(0x00f0);
    if (directions == PORTABLE_FRONTEND_BUTTON_UP)
        return 0;
    if (directions == PORTABLE_FRONTEND_BUTTON_DOWN)
        return 1;
    if (directions == PORTABLE_FRONTEND_BUTTON_LEFT)
        return 2;
    if (directions == PORTABLE_FRONTEND_BUTTON_RIGHT)
        return 3;
    return -1;
}

uint8_t portable_frontend_flow_driver_pages(const PortableFrontendFlow *flow)
{
    return flow == NULL ? 0u
                        : (uint8_t)(flow->config.driver_count / PORTABLE_FRONTEND_DRIVER_SLOTS);
}

const PortableFrontendDriverSlot *portable_frontend_flow_driver_at(const PortableFrontendFlow *flow,
                                                                   uint8_t page, uint8_t slot)
{
    const size_t index = (size_t)page * PORTABLE_FRONTEND_DRIVER_SLOTS + slot;
    if (flow == NULL || slot >= PORTABLE_FRONTEND_DRIVER_SLOTS ||
        index >= flow->config.driver_count)
        return NULL;
    return &flow->config.drivers[index];
}

const PortableFrontendDriverSlot *
portable_frontend_flow_find_driver(const PortableFrontendFlow *flow, uint32_t content_id)
{
    if (flow == NULL || content_id == 0u)
        return NULL;
    for (size_t index = 0u; index < flow->config.driver_count; ++index)
        if (flow->config.drivers[index].content_id == content_id)
            return &flow->config.drivers[index];
    return NULL;
}

uint32_t portable_frontend_flow_voice_id(const PortableFrontendFlow *flow, uint32_t content_id)
{
    const PortableFrontendDriverSlot *slot = portable_frontend_flow_find_driver(flow, content_id);
    if (slot == NULL)
        return content_id;
    return slot->voice_id != 0u ? slot->voice_id : slot->content_id;
}

static uint8_t seat_slot(const PortableFrontendFlow *flow, uint8_t page, uint8_t slot)
{
    const PortableFrontendDriverSlot *driver = portable_frontend_flow_driver_at(flow, page, slot);
    if (driver == NULL || driver->content_id == 0u)
        return PORTABLE_FRONTEND_RANDOM_SLOT;
    return page == 0u ? slot : driver->seat_slot;
}

uint8_t portable_frontend_flow_page_platform(const PortableFrontendFlow *flow, uint8_t page)
{
    const PortableFrontendDriverSlot *driver = portable_frontend_flow_driver_at(flow, page, 0u);
    return driver != NULL ? driver->platform : 0u;
}

static uint8_t group_platform(const PortableFrontendFlow *flow, uint8_t group)
{
    return flow->mesh_group_platforms != NULL && group != 0u && group <= flow->mesh_group_count
               ? flow->mesh_group_platforms[group - 1u]
               : 0u;
}

int portable_frontend_flow_group_allowed(const PortableFrontendFlow *flow, uint8_t group)
{
    return flow != NULL && (group == 0u ? flow->platform == 0u
                                        : group <= flow->mesh_group_count &&
                                              group_platform(flow, group) == flow->platform);
}

uint8_t portable_frontend_flow_platform_group(const PortableFrontendFlow *flow, uint8_t platform)
{
    for (uint8_t group = 1u; flow != NULL && platform != 0u && group <= flow->mesh_group_count; ++group)
        if (group_platform(flow, group) == platform)
            return group;
    return 0u;
}

/* Random takes the first available driver of the page's platform; on the
 * base game it never lands on a 3D-only driver, which 2D courses refuse. */
static uint32_t random_driver(const PortableFrontendFlow *flow, uint8_t platform)
{
    size_t index;
    for (index = 0u; index < flow->config.driver_count; ++index)
        if (flow->config.drivers[index].available != 0u &&
            flow->config.drivers[index].content_id != 0u &&
            flow->config.drivers[index].platform == platform &&
            (platform != 0u || !flow->config.drivers[index].mesh_only))
            return flow->config.drivers[index].content_id;
    return 0u;
}

/* The next page (step +1/-1) a player may browse: every page for the first
 * player, the locked platform's pages for the others. */
static uint8_t step_page(const PortableFrontendFlow *flow, uint8_t page, int step)
{
    const uint8_t pages = portable_frontend_flow_driver_pages(flow);
    for (uint8_t attempt = 1u; attempt <= pages; ++attempt)
    {
        const uint8_t next = (uint8_t)((page + pages + step * (int)attempt) % pages);
        if (flow->character_player == 0u ||
            portable_frontend_flow_page_platform(flow, next) == flow->platform)
            return next;
    }
    return page;
}

static uint8_t first_platform_page(const PortableFrontendFlow *flow, uint8_t platform)
{
    for (uint8_t page = 0u; page < portable_frontend_flow_driver_pages(flow); ++page)
        if (portable_frontend_flow_page_platform(flow, page) == platform)
            return page;
    return 0u;
}

/* A chosen 3D-only driver races only on 3D courses (the shared selector). */
static int mesh_only_selected(const PortableFrontendFlow *flow)
{
    for (uint8_t player = 0u; player < (flow->players > 1u ? flow->players : 1u); ++player)
    {
        const PortableFrontendDriverSlot *driver = portable_frontend_flow_find_driver(
            flow, flow->players > 1u ? flow->player_characters[player] : flow->selected_character);
        if (driver != NULL && driver->mesh_only)
            return 1;
    }
    return 0;
}

static const PortableFrontendCourseSlot *course_slot(const PortableFrontendFlow *flow, uint8_t cup,
                                                     uint8_t track)
{
    size_t index;
    for (index = 0u; index < flow->config.course_count; ++index)
        if (flow->config.courses[index].cup == cup && flow->config.courses[index].track == track)
            return &flow->config.courses[index];
    return NULL;
}

uint32_t portable_frontend_flow_mesh_mode(const PortableFrontendFlow *flow)
{
    if (flow == NULL)
        return 0u;
    return flow->mode_cursor == 0u ? 1u
         : flow->mode_cursor == 1u ? 2u
         : flow->mode_cursor == 2u ? (flow->players > 1u ? 8u : 4u)
                                   : 16u;
}

static int mesh_row_open(const PortableFrontendFlow *flow, const PortableFrontendMeshCup *cup,
                         unsigned row)
{
    return row < cup->course_count &&
           (cup->course_modes[row] & portable_frontend_flow_mesh_mode(flow)) != 0u;
}

int portable_frontend_flow_mesh_cup_open(const PortableFrontendFlow *flow, size_t index)
{
    if (flow == NULL || index >= flow->mesh_cup_count)
        return 0;
    const PortableFrontendMeshCup *cup = &flow->mesh_cups[index];
    if (flow->mode_cursor == 0u)
        return cup->gp != 0u;
    for (unsigned row = 0u; row < cup->course_count; ++row)
        if (mesh_row_open(flow, cup, row))
            return 1;
    return 0;
}

int portable_frontend_flow_mesh_group_open(const PortableFrontendFlow *flow, uint8_t group)
{
    if (flow == NULL || group == 0u || group > flow->mesh_group_count)
        return 0;
    for (size_t index = 0u; index < flow->mesh_cup_count; ++index)
        if (flow->mesh_cups[index].group + 1u == group &&
            portable_frontend_flow_mesh_cup_open(flow, index))
            return 1;
    return 0;
}

/* The first open cup of a group at or after start, wrapping in config order. */
static int mesh_first_cup(const PortableFrontendFlow *flow, uint8_t group, size_t start, int step,
                          uint8_t *out)
{
    const size_t count = flow->mesh_cup_count;
    for (size_t attempt = 0u; attempt < count; ++attempt)
    {
        const size_t index = (start + count + (size_t)step * attempt) % count;
        if (flow->mesh_cups[index].group + 1u == group &&
            portable_frontend_flow_mesh_cup_open(flow, index))
        {
            *out = (uint8_t)index;
            return 1;
        }
    }
    return 0;
}

static uint8_t mesh_first_row(const PortableFrontendFlow *flow, const PortableFrontendMeshCup *cup)
{
    for (uint8_t row = 0u; row < cup->course_count; ++row)
        if (mesh_row_open(flow, cup, row))
            return row;
    return 0u;
}

unsigned portable_frontend_flow_records_list(const PortableFrontendFlow *flow,
                                             uint8_t entries[PORTABLE_FRONTEND_RECORDS_MAX])
{
    unsigned count = 0u;
    if (flow == NULL || entries == NULL)
        return 0u;
    const uint8_t open = portable_frontend_flow_cup_access(flow);
    const uint8_t first = portable_track_set_first_cup(portable_track_set_of_cup(flow->course_cup));
    for (uint8_t cup = 0u; cup < PORTABLE_FRONTEND_CUPS; ++cup)
        for (uint8_t track = 0u; (open & (1u << cup)) != 0u && track < 4u; ++track)
        {
            const PortableFrontendCourseSlot *slot =
                course_slot(flow, (uint8_t)(first + cup), track);
            if (slot != NULL && slot->available != 0u && count < PORTABLE_FRONTEND_RECORDS_MAX)
                entries[count++] = (uint8_t)((first + cup) * 4u + track);
        }
    return count;
}

/* Selects list entry index, scrolling the eight rows only as far as needed. */
static void records_select(PortableFrontendFlow *flow, const uint8_t *entries, unsigned index)
{
    flow->course_cup = (uint8_t)(entries[index] / 4u);
    flow->course_track = (uint8_t)(entries[index] % 4u);
    const PortableFrontendCourseSlot *slot =
        course_slot(flow, flow->course_cup, flow->course_track);
    flow->selected_course = slot != NULL ? slot->content_id : 0u;
    if (index < flow->records_top)
        flow->records_top = (uint8_t)index;
    if (index >= (unsigned)flow->records_top + PORTABLE_FRONTEND_RECORDS_ROWS)
        flow->records_top = (uint8_t)(index + 1u - PORTABLE_FRONTEND_RECORDS_ROWS);
}

/* Opens the list on the current cup and track when listed, else its first row. */
static void records_open(PortableFrontendFlow *flow)
{
    uint8_t entries[PORTABLE_FRONTEND_RECORDS_MAX];
    const unsigned count = portable_frontend_flow_records_list(flow, entries);
    unsigned index = 0u;
    while (index < count && entries[index] != flow->course_cup * 4u + flow->course_track)
        ++index;
    flow->records_top = 0u;
    flow->selected_course = 0u;
    if (count != 0u)
        records_select(flow, entries, index < count ? index : 0u);
}

static int update_input(PortableFrontendFlow *flow, uint16_t buttons)
{
    if (!valid_buttons(buttons))
        return 0;
    flow->triggered = buttons & (uint16_t)~flow->buttons;
    flow->repeated = flow->triggered;
    if (buttons != 0u && buttons == flow->buttons)
    {
        if (flow->hold_ticks != UINT16_MAX)
            ++flow->hold_ticks;
        if (flow->hold_ticks == PORTABLE_FRONTEND_REPEAT_DELAY ||
            (flow->hold_ticks > PORTABLE_FRONTEND_REPEAT_DELAY &&
             (flow->hold_ticks - PORTABLE_FRONTEND_REPEAT_DELAY) %
                     PORTABLE_FRONTEND_REPEAT_PERIOD ==
                 0u))
            flow->repeated = buttons;
        else
            flow->repeated = 0u;
    }
    else
        flow->hold_ticks = 0u;
    flow->buttons = buttons;
    return 1;
}

int portable_frontend_flow_init(PortableFrontendFlow *flow, const PortableFrontendConfig *config)
{
    size_t index;
    if (flow == NULL || config == NULL || config->drivers == NULL || config->courses == NULL ||
        config->driver_count == 0u || config->driver_count > PORTABLE_FRONTEND_DRIVER_CAPACITY ||
        config->driver_count % PORTABLE_FRONTEND_DRIVER_SLOTS != 0u || config->course_count == 0u)
        return 0;
    for (index = 0u; index < config->driver_count; ++index)
    {
        const size_t slot = index % PORTABLE_FRONTEND_DRIVER_SLOTS;
        const PortableFrontendDriverSlot *driver = &config->drivers[index];
        if ((slot == PORTABLE_FRONTEND_RANDOM_SLOT) != (driver->content_id == 0u) &&
            (index < PORTABLE_FRONTEND_DRIVER_SLOTS || slot == PORTABLE_FRONTEND_RANDOM_SLOT))
            return 0;
        if (index >= PORTABLE_FRONTEND_DRIVER_SLOTS && driver->content_id != 0u &&
            (driver->seat_slot >= PORTABLE_FRONTEND_DRIVER_SLOTS ||
             driver->seat_slot == PORTABLE_FRONTEND_RANDOM_SLOT ||
             driver->art_slot >= PORTABLE_FRONTEND_DRIVER_SLOTS ||
             driver->art_slot == PORTABLE_FRONTEND_RANDOM_SLOT))
            return 0;
    }
    for (index = 0u; index < config->course_count; ++index)
        if (config->courses[index].content_id == 0u ||
            config->courses[index].cup >= PORTABLE_FRONTEND_COURSE_CUPS ||
            config->courses[index].track >= PORTABLE_FRONTEND_TRACKS_PER_CUP)
            return 0;
    memset(flow, 0, sizeof *flow);
    flow->config = *config;
    flow->page = PORTABLE_FRONTEND_PAGE_TITLE_INTRO;
    flow->character_cursor = 4u;
    flow->players = 1u;
    return emit_event(flow, PORTABLE_FRONTEND_EVENT_PAGE_ENTER, PORTABLE_FRONTEND_PAGE_TITLE_INTRO);
}

int portable_frontend_flow_set_mesh_cups(PortableFrontendFlow *flow,
    const PortableFrontendMeshCup *cups, size_t count, const char *const *group_names,
    const uint8_t *group_platforms, uint8_t group_count)
{
    if (flow == NULL || count > PORTABLE_FRONTEND_MESH_CUP_CAPACITY ||
        group_count > PORTABLE_FRONTEND_MESH_GROUP_CAPACITY || (count != 0u && cups == NULL) ||
        (group_count != 0u && group_names == NULL))
        return 0;
    for (size_t index = 0u; index < count; ++index)
    {
        const PortableFrontendMeshCup *cup = &cups[index];
        if (cup->group >= group_count || cup->course_count == 0u ||
            cup->course_count > PORTABLE_FRONTEND_MESH_CUP_COURSES)
            return 0;
        for (unsigned row = 0u; row < cup->course_count; ++row)
            if (cup->course_ids[row] == 0u)
                return 0;
    }
    flow->mesh_cups = count != 0u ? cups : NULL;
    flow->mesh_cup_count = count;
    flow->mesh_group_names = group_count != 0u ? group_names : NULL;
    flow->mesh_group_platforms = group_count != 0u ? group_platforms : NULL;
    flow->mesh_group_count = group_count;
    flow->mesh_group = flow->mesh_cup = flow->mesh_request = 0u;
    flow->mesh_request_course = 0u;
    return 1;
}

int portable_frontend_flow_settle_title(PortableFrontendFlow *flow)
{
    if (flow == NULL || flow->page != PORTABLE_FRONTEND_PAGE_TITLE_INTRO ||
        flow->transition_ticks != 0u)
        return 0;
    if (!enter_page(flow, PORTABLE_FRONTEND_PAGE_TITLE_MENU))
        return 0;
    flow->page_ticks = 70u;
    return 1;
}

/* title_handleNightStars precedes title_handleNightKoopaShip. This private
 * presentation RNG starts deterministically and never touches race RNG. */
static void tick_title_ambient(PortableFrontendFlow *flow)
{
    if (flow->page != PORTABLE_FRONTEND_PAGE_TITLE_MENU || flow->title_variant != 2u)
        return;
    switch (flow->title_star_state)
    {
    case 0u:
        if (++flow->title_star_counter > flow->title_star_wait)
            flow->title_star_state = 1u;
        break;
    case 1u:
        flow->title_star_counter = 0u;
        flow->title_star_state = 2u;
        break;
    case 2u:
        if (++flow->title_star_counter > 32u)
        {
            flow->title_star_counter = 0u;
            flow->title_star_wait =
                (uint16_t)((portable_source_random(&flow->title_ambient_rng) & 255u) + 255u);
            flow->title_star_state = 0u;
        }
        break;
    default:
        break;
    }
    if (flow->title_ship_ticks != UINT32_MAX)
        ++flow->title_ship_ticks;
}

static int tick_title_intro_page(PortableFrontendFlow *flow, uint16_t confirm)
{
    if (((flow->triggered & confirm) != 0u &&
         (flow->short_transitions || flow->page_ticks > 32u)) ||
        flow->page_ticks > 480u)
        return begin_transition(flow, PORTABLE_FRONTEND_PAGE_TITLE_MENU,
                                PORTABLE_FRONTEND_EVENT_ACCEPT);
    return flow->event_overflow == 0u;
}

static int tick_title_menu_page(PortableFrontendFlow *flow, int direction, uint16_t confirm)
{
    if (flow->player_count_open)
    {
        const uint8_t most = flow->max_players >= 2u ? flow->max_players : 2u;
        uint8_t count = (uint8_t)(flow->players - 2u);
        const uint8_t previous_count = count;
        /* The count reads like the title list: Up or Right adds a player. */
        if ((direction == 1 || direction == 2) &&
            !move_clamped(flow, &count, (uint8_t)(most - 1u), -1))
            return 0;
        if ((direction == 0 || direction == 3) &&
            !move_clamped(flow, &count, (uint8_t)(most - 1u), 1))
            return 0;
        flow->players = (uint8_t)(count + 2u);
        if (count != previous_count)
            return flow->event_overflow == 0u;
        if ((flow->triggered & confirm) != 0u)
        {
            flow->player_count_open = 0u;
            flow->mode_cursor = 2u;
            flow->mode_phase = 0u;
            return begin_transition(flow, PORTABLE_FRONTEND_PAGE_MODE,
                                    PORTABLE_FRONTEND_EVENT_ACCEPT);
        }
        if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_B) != 0u)
        {
            flow->player_count_open = 0u;
            flow->players = 1u;
            /* Source submenu changes reset the shared list interpolation. */
            flow->title_cursor_motion.y = (int16_t)(132 - 16 * (int)flow->title_cursor);
            flow->title_cursor_motion.phase = flow->title_cursor_motion.ticks = 0u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_BACK, flow->title_cursor);
        }
        return flow->event_overflow == 0u;
    }
    /* Rows: single player, multiplayer, ghost exchange, then the
     * native-only Options row the platform shell opens itself. */
    const uint8_t title_before = flow->title_cursor;
    if (direction == 0 && !move_clamped(flow, &flow->title_cursor, 4u, -1))
        return 0;
    if (direction == 1 && !move_clamped(flow, &flow->title_cursor, 4u, 1))
        return 0;
    /* Source state25 consumes a changed cursor before considering confirm. */
    if (flow->title_cursor != title_before)
        return flow->event_overflow == 0u;
    if ((flow->triggered & confirm) != 0u && (flow->short_transitions || flow->page_ticks > 69u))
    {
        if (flow->title_cursor == 1u)
        {
            flow->player_count_open = 1u;
            flow->players = 2u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_OPEN, flow->players);
        }
        if (flow->title_cursor == 2u)
        {
            /* Ghost exchange is the Time Trial records ghost view
             * (export a copy, receive a file) rather than a link cable. */
            flow->players = 1u;
            flow->mode_cursor = 1u;
            flow->mode_phase = 0u;
            flow->time_trial_cursor = 1u;
            return begin_transition(flow, PORTABLE_FRONTEND_PAGE_RECORDS,
                                    PORTABLE_FRONTEND_EVENT_ACCEPT);
        }
        if (flow->title_cursor != 0u)
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_UNAVAILABLE, flow->title_cursor);
        return begin_transition(flow, PORTABLE_FRONTEND_PAGE_MODE, PORTABLE_FRONTEND_EVENT_ACCEPT);
    }
    return flow->event_overflow == 0u;
}

static int tick_mode_page(PortableFrontendFlow *flow, int direction, uint16_t confirm)
{
    /* 4860 opens Quick Run, Time Trial or VS options with Select. 58B0
     * wraps the rows, toggles with either horizontal button and closes
     * on A/B/Start; closing keeps the edited values. */
    if (flow->quick_options_open)
    {
        const int vs = flow->players > 1u;
        const unsigned rows = vs ? 4u : flow->mode_cursor == 1u ? 2u : 3u;
        if (direction == 0 || direction == 1)
        {
            flow->quick_options_cursor =
                (uint8_t)((flow->quick_options_cursor + (direction == 0 ? rows - 1u : 1u)) % rows);
            if (!emit_event(flow, PORTABLE_FRONTEND_EVENT_MOVE, flow->quick_options_cursor))
                return 0;
        }
        if (direction == 2 || direction == 3)
        {
            if (flow->mode_cursor == 1u)
            {
                flow->ghost_display_off ^= (uint8_t)(1u << flow->quick_options_cursor);
                return emit_event(flow, PORTABLE_FRONTEND_EVENT_OPEN,
                                  (flow->ghost_display_off >> flow->quick_options_cursor) & 1u);
            }
            PortableQuickRunOptions *options = vs ? &flow->vs_options : &flow->quick_run_options;
            /* VS rows: laps, items, mushroom start, coins. */
            uint8_t *value = flow->quick_options_cursor == 0u         ? &options->five_laps
                             : flow->quick_options_cursor == 1u       ? &options->items_off
                             : flow->quick_options_cursor == 2u && vs ? &options->mushroom_start
                                                                      : &options->coins_off;
            *value ^= 1u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_OPEN, *value);
        }
        if ((flow->triggered & (confirm | PORTABLE_FRONTEND_BUTTON_B)) != 0u)
        {
            flow->quick_options_open = 0u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_ACCEPT, 0u);
        }
        return flow->event_overflow == 0u;
    }
    if (((flow->players == 1u && flow->mode_cursor == 1u) || flow->mode_cursor == 2u) &&
        flow->mode_phase == 0u && (flow->triggered & PORTABLE_FRONTEND_BUTTON_SELECT) != 0u)
    {
        flow->quick_options_open = 1u;
        flow->quick_options_cursor = 0u;
        return emit_event(flow, PORTABLE_FRONTEND_EVENT_OPEN, 0u);
    }
    if (flow->mode_phase == 0u && flow->players > 1u)
    {
        /* Local multiplayer offers only VS (2) and Battle (3); any
         * direction steps across the pair. */
        if (direction >= 0)
        {
            flow->mode_cursor = flow->mode_cursor == 3u ? 2u : 3u;
            if (!emit_event(flow, PORTABLE_FRONTEND_EVENT_MOVE, flow->mode_cursor))
                return 0;
        }
    }
    else if (flow->mode_phase == 0u)
    {
        if (!move_wrapped(flow, &flow->mode_cursor, 4u, direction))
            return 0;
    }
    else
    {
        if (!move_wrapped(flow, &flow->race_speed, 3u, direction))
            return 0;
    }
    if ((flow->triggered & confirm) != 0u)
    {
        if (flow->mode_phase != 0u)
        {
            return begin_transition(flow, PORTABLE_FRONTEND_PAGE_CHARACTER,
                                    PORTABLE_FRONTEND_EVENT_ACCEPT);
        }
        if (flow->mode_cursor != 1u)
        {
            if (!begin_transition(flow, PORTABLE_FRONTEND_PAGE_MODE, PORTABLE_FRONTEND_EVENT_OPEN))
                return 0;
            flow->mode_phase = 1u;
            flow->race_speed = 0u;
            return 1;
        }
        return begin_transition(flow, PORTABLE_FRONTEND_PAGE_TIME_TRIAL,
                                PORTABLE_FRONTEND_EVENT_ACCEPT);
    }
    if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_B) != 0u)
    {
        if (flow->mode_phase != 0u)
        {
            if (!begin_transition(flow, PORTABLE_FRONTEND_PAGE_MODE, PORTABLE_FRONTEND_EVENT_BACK))
                return 0;
            flow->mode_phase = 0u;
            return 1;
        }
        return begin_transition(flow, PORTABLE_FRONTEND_PAGE_TITLE_INTRO,
                                PORTABLE_FRONTEND_EVENT_BACK);
    }
    return flow->event_overflow == 0u;
}

static int tick_time_trial_page(PortableFrontendFlow *flow, int direction, uint16_t confirm)
{
    if (!move_wrapped(flow, &flow->time_trial_cursor, 2u, direction))
        return 0;
    if ((flow->triggered & confirm) != 0u)
    {
        if (flow->time_trial_cursor == 1u)
            return begin_transition(flow, PORTABLE_FRONTEND_PAGE_RECORDS,
                                    PORTABLE_FRONTEND_EVENT_ACCEPT);
        return begin_transition(flow, PORTABLE_FRONTEND_PAGE_CHARACTER,
                                PORTABLE_FRONTEND_EVENT_ACCEPT);
    }
    if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_B) != 0u)
        return begin_transition(flow, PORTABLE_FRONTEND_PAGE_MODE, PORTABLE_FRONTEND_EVENT_BACK);
    return flow->event_overflow == 0u;
}

static int tick_character_page(PortableFrontendFlow *flow, int direction)
{
    if (direction >= 0 && flow->character_locked == 0u)
    {
        const uint8_t pages = portable_frontend_flow_driver_pages(flow);
        const uint8_t column = (uint8_t)(flow->character_cursor % 3u);
        uint8_t next = character_neighbors[flow->character_cursor][direction];
        uint8_t page = flow->character_page;
        /* With roster pages, leaving a side column turns the page
         * instead of wrapping; one page keeps the original wrap. */
        if (pages > 1u && direction == 2 && column == 0u)
            page = step_page(flow, page, -1);
        else if (pages > 1u && direction == 3 && column == 2u)
            page = step_page(flow, page, 1);
        if (next != flow->character_cursor || page != flow->character_page)
        {
            flow->character_cursor = next;
            flow->character_page = page;
            if (!emit_event(flow, PORTABLE_FRONTEND_EVENT_MOVE, next))
                return 0;
        }
    }
    /* 7138 case 2: Select honks D92E8[cursor] except on random. */
    if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_SELECT) != 0u && flow->character_locked == 0u &&
        seat_slot(flow, flow->character_page, flow->character_cursor) !=
            PORTABLE_FRONTEND_RANDOM_SLOT &&
        !emit_event(flow, PORTABLE_FRONTEND_EVENT_HORN,
                    seat_slot(flow, flow->character_page, flow->character_cursor)))
        return 0;
    /* 7138: R hops (60AC 10..12) and L spins (6850) an idle kart. */
    if ((flow->triggered & (PORTABLE_FRONTEND_BUTTON_L | PORTABLE_FRONTEND_BUTTON_R)) != 0u &&
        flow->tick >= flow->character_trick_until &&
        (flow->character_locked != 0u ||
         seat_slot(flow, flow->character_page, flow->character_cursor) !=
             PORTABLE_FRONTEND_RANDOM_SLOT))
    {
        const uint32_t spin = (flow->triggered & PORTABLE_FRONTEND_BUTTON_R) == 0u;
        flow->character_trick_until = flow->tick + (spin ? 16u : 19u);
        if (!emit_event(flow, PORTABLE_FRONTEND_EVENT_KART_TRICK, spin))
            return 0;
    }
    if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_A) != 0u && flow->character_locked == 0u)
    {
        const PortableFrontendDriverSlot *slot =
            portable_frontend_flow_driver_at(flow, flow->character_page, flow->character_cursor);
        uint32_t selected = slot != NULL ? slot->content_id : 0u;
        const uint8_t platform = portable_frontend_flow_page_platform(flow, flow->character_page);
        if (flow->character_cursor == 4u)
            selected = random_driver(flow, platform);
        if (selected == 0u || (flow->character_cursor != 4u && slot->available == 0u))
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_UNAVAILABLE, flow->character_cursor);
        if (flow->players > 1u)
        {
            /* Players lock in turn; the next player starts on Random. */
            flow->player_characters[flow->character_player] = selected;
            if (flow->character_player == 0u)
            {
                flow->selected_character = selected;
                flow->platform = platform;
            }
            if (flow->character_player + 1u < flow->players)
            {
                ++flow->character_player;
                flow->character_cursor = PORTABLE_FRONTEND_RANDOM_SLOT;
                flow->character_page = first_platform_page(flow, flow->platform);
            }
            else
                flow->character_locked = 1u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_CHARACTER_LOCK, selected);
        }
        flow->selected_character = selected;
        flow->platform = platform;
        flow->character_locked = 1u;
        lock_kart(flow);
        return emit_event(flow, PORTABLE_FRONTEND_EVENT_CHARACTER_LOCK, selected);
    }
    if ((direction == 2 || direction == 3) && flow->character_locked != 0u && karts_open(flow))
    {
        flow->kart_cursor = (uint8_t)((flow->kart_cursor + (direction == 3 ? 1u : flow->kart_count - 1u)) %
                                      flow->kart_count);
        flow->selected_kart = flow->karts[flow->kart_cursor].content_id;
        if (!emit_event(flow, PORTABLE_FRONTEND_EVENT_MOVE, flow->kart_cursor))
            return 0;
    }
    /* As in the ROM, A confirms a locked driver like Start. */
    if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_START) != 0u ||
        ((flow->triggered & PORTABLE_FRONTEND_BUTTON_A) != 0u && flow->character_locked != 0u))
    {
        if (flow->character_locked == 0u)
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_UNAVAILABLE, flow->character_cursor);
        return begin_transition(flow, PORTABLE_FRONTEND_PAGE_COURSE,
                                PORTABLE_FRONTEND_EVENT_ACCEPT);
    }
    if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_B) != 0u)
    {
        if (flow->players > 1u && (flow->character_locked != 0u || flow->character_player != 0u))
        {
            if (flow->character_locked == 0u)
                --flow->character_player;
            flow->character_locked = 0u;
            flow->player_characters[flow->character_player] = 0u;
            if (flow->character_player == 0u)
                flow->selected_character = flow->platform = 0u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_BACK, PORTABLE_FRONTEND_PAGE_CHARACTER);
        }
        if (flow->character_locked != 0u)
        {
            flow->character_locked = 0u;
            flow->selected_character = flow->platform = 0u;
            flow->selected_kart = 0u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_BACK, PORTABLE_FRONTEND_PAGE_CHARACTER);
        }
        return begin_transition(flow,
                                flow->mode_cursor != 1u ? PORTABLE_FRONTEND_PAGE_MODE
                                                        : PORTABLE_FRONTEND_PAGE_TIME_TRIAL,
                                PORTABLE_FRONTEND_EVENT_BACK);
    }
    return flow->event_overflow == 0u;
}

static int tick_records_page(PortableFrontendFlow *flow, int direction, uint16_t confirm)
{
    if (flow->course_phase == 3u || flow->course_phase == 5u || flow->course_phase == 6u)
    {
        if (direction == 2 || direction == 3)
        {
            flow->record_erase_choice = (uint8_t)(direction == 3);
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_MOVE, flow->record_erase_choice);
        }
        if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_B) != 0u)
        {
            flow->course_phase = flow->course_phase >= 5u ? 4u : 2u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_BACK, flow->selected_course);
        }
        if ((flow->triggered & confirm) != 0u)
        {
            const unsigned phase = flow->course_phase;
            const int received = flow->record_ghost_slot != 0u;
            flow->course_phase = phase >= 5u ? 4u : 2u;
            const PortableFrontendEventType action =
                phase == 6u   ? PORTABLE_FRONTEND_EVENT_GHOST_RECEIVE
                : phase == 3u ? PORTABLE_FRONTEND_EVENT_RECORD_ERASE
                : received    ? PORTABLE_FRONTEND_EVENT_RECEIVED_GHOST_ERASE
                              : PORTABLE_FRONTEND_EVENT_GHOST_ERASE;
            return emit_event(
                flow, flow->record_erase_choice ? action : PORTABLE_FRONTEND_EVENT_BACK,
                phase >= 5u && received ? flow->record_ghost_slot : flow->selected_course);
        }
        return flow->event_overflow == 0u;
    }
    if (flow->course_phase == 4u)
    {
        if (direction == 2 || direction == 3)
        {
            flow->record_ghost_slot =
                (uint8_t)((flow->record_ghost_slot + (direction == 3 ? 1u : 2u)) % 3u);
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_MOVE, flow->record_ghost_slot);
        }
        if ((flow->triggered & confirm) != 0u)
        {
            if (flow->record_ghost_slot == 0u)
                return emit_event(flow, PORTABLE_FRONTEND_EVENT_GHOST_EXPORT,
                                  flow->selected_course);
            flow->course_phase = 6u;
            flow->record_erase_choice = 0u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_OPEN, flow->record_ghost_slot);
        }
    }
    if (flow->course_phase == 2u && (direction == 0 || direction == 1))
    {
        uint8_t entries[PORTABLE_FRONTEND_RECORDS_MAX];
        const unsigned count = portable_frontend_flow_records_list(flow, entries);
        unsigned index = 0u;
        while (index < count && entries[index] != flow->course_cup * 4u + flow->course_track)
            ++index;
        if (index >= count || (direction == 0 ? index == 0u : index + 1u >= count))
            return 1;
        records_select(flow, entries, direction == 0 ? index - 1u : index + 1u);
        return emit_event(flow, PORTABLE_FRONTEND_EVENT_MOVE, flow->selected_course);
    }
    /* L/R step GBA -> Extra -> Custom past sets with nothing listed. */
    if (flow->course_phase == 2u &&
        (flow->triggered & (PORTABLE_FRONTEND_BUTTON_L | PORTABLE_FRONTEND_BUTTON_R)) != 0u)
    {
        static const PortableTrackSet order[3] = {PORTABLE_TRACK_SET_GBA, PORTABLE_TRACK_SET_EXTRA,
                                                  PORTABLE_TRACK_SET_CUSTOM};
        const PortableTrackSet current = portable_track_set_of_cup(flow->course_cup);
        const unsigned step = (flow->triggered & PORTABLE_FRONTEND_BUTTON_R) != 0u ? 1u : 2u;
        unsigned position = 0u;
        while (position < 3u && order[position] != current)
            ++position;
        for (unsigned attempt = 1u; position < 3u && attempt < 3u; ++attempt)
        {
            uint8_t entries[PORTABLE_FRONTEND_RECORDS_MAX];
            PortableFrontendFlow probe = *flow;
            probe.course_cup =
                portable_track_set_first_cup(order[(position + step * attempt) % 3u]);
            if (portable_frontend_flow_records_list(&probe, entries) == 0u)
                continue;
            flow->course_cup = probe.course_cup;
            flow->course_track = 0u;
            records_open(flow);
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_TRACK_SET,
                              (uint32_t)flow->course_cup * 4u);
        }
        return 1;
    }
    if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_SELECT) != 0u)
    {
        flow->record_erase_choice = 0u;
        flow->course_phase = flow->course_phase == 4u ? 5u : 3u;
        return emit_event(flow, PORTABLE_FRONTEND_EVENT_OPEN, flow->selected_course);
    }
    if ((flow->triggered & confirm) != 0u && flow->course_phase == 2u)
    {
        flow->course_phase = 4u;
        flow->record_ghost_slot = 0u;
        return emit_event(flow, PORTABLE_FRONTEND_EVENT_OPEN, flow->selected_course);
    }
    if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_B) != 0u)
    {
        if (flow->course_phase == 4u)
        {
            flow->course_phase = 2u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_BACK, flow->selected_course);
        }
        return begin_transition(flow, PORTABLE_FRONTEND_PAGE_TIME_TRIAL,
                                PORTABLE_FRONTEND_EVENT_BACK);
    }
    return flow->event_overflow == 0u;
}

/* B89C L/R: GBA -> Extra -> Custom (Battle: the arena group), then every
 * 3D cup group with something for the selected mode, skipping empty or
 * locked entries. Leaving a 3D group restores the remembered 2D cup. */
static int tick_course_track_set(PortableFrontendFlow *flow)
{
    static const PortableTrackSet order[3] = {PORTABLE_TRACK_SET_GBA, PORTABLE_TRACK_SET_EXTRA,
                                              PORTABLE_TRACK_SET_CUSTOM};
    const int battle = flow->mode_cursor == 3u;
    const unsigned sets = battle ? 1u : 3u;
    const unsigned total = sets + flow->mesh_group_count;
    const PortableTrackSet current = portable_track_set_of_cup(flow->course_cup);
    const uint8_t cup = battle ? 0u
        : (uint8_t)(flow->course_cup - portable_track_set_first_cup(current));
    const unsigned step = (flow->triggered & PORTABLE_FRONTEND_BUTTON_L) != 0u &&
                                  (flow->triggered & (PORTABLE_FRONTEND_BUTTON_R |
                                                      PORTABLE_FRONTEND_BUTTON_SELECT)) == 0u
                              ? total - 1u
                              : 1u;
    unsigned position = 0u;
    if (flow->mesh_group != 0u)
        position = sets + flow->mesh_group - 1u;
    else if (!battle)
        while (position < 3u && order[position] != current)
            ++position;
    for (unsigned attempt = 1u; position < total && attempt < total; ++attempt)
    {
        const unsigned next = (position + step * attempt) % total;
        if (next >= sets)
        {
            uint8_t first;
            const uint8_t group = (uint8_t)(next - sets + 1u);
            if (!portable_frontend_flow_group_allowed(flow, group) ||
                !portable_frontend_flow_mesh_group_open(flow, group) ||
                !mesh_first_cup(flow, group, 0u, 1, &first))
                continue;
            flow->mesh_group = group;
            flow->mesh_cup = first;
            flow->course_track = 0u;
            flow->course_phase = 0u;
            flow->gp_preview_track = flow->gp_preview_ticks = 0u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_TRACK_SET,
                              (uint32_t)flow->course_cup * 4u);
        }
        if (!portable_frontend_flow_group_allowed(flow, 0u))
            continue;
        if (battle)
        {
            flow->mesh_group = 0u;
            flow->course_track = 0u;
            flow->course_phase = 1u;
            clamp_cup(flow);
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_TRACK_SET,
                              (uint32_t)flow->course_cup * 4u);
        }
        int present = 0;
        for (size_t index = 0u; index < flow->config.course_count; ++index)
            present |= portable_track_set_of_cup(flow->config.courses[index].cup) == order[next];
        PortableFrontendFlow probe = *flow;
        probe.course_cup = portable_track_set_first_cup(order[next]);
        if (!present || portable_frontend_flow_cup_access(&probe) == 0u)
            continue;
        flow->mesh_group = 0u;
        flow->course_cup = (uint8_t)(probe.course_cup + cup);
        flow->course_track = 0u;
        flow->course_phase = 0u;
        clamp_cup(flow);
        return emit_event(flow, PORTABLE_FRONTEND_EVENT_TRACK_SET, (uint32_t)flow->course_cup * 4u);
    }
    if (battle && flow->mesh_group == 0u && flow->course_phase == 0u)
    {
        /* The arena group keeps its stock cue when Extra is open. */
        int has_extra = 0;
        for (size_t index = 0u; index < flow->config.course_count; ++index)
            has_extra |= portable_track_set_of_cup(flow->config.courses[index].cup) ==
                         PORTABLE_TRACK_SET_EXTRA;
        if (current != PORTABLE_TRACK_SET_ARENA || !has_extra ||
            flow->extra_cup_access[flow->race_speed] == 0u)
            return 1;
        flow->course_track = 0u;
        clamp_cup(flow);
        return emit_event(flow, PORTABLE_FRONTEND_EVENT_TRACK_SET, (uint32_t)flow->course_cup * 4u);
    }
    return 1;
}

static int mesh_request(PortableFrontendFlow *flow, const PortableFrontendMeshCup *cup, unsigned row,
                        int explore)
{
    if (row >= cup->course_count)
        return 0;
    flow->mesh_request = (uint8_t)(explore || (cup->explore_only & (1u << row))
                                       ? PORTABLE_FRONTEND_MESH_EXPLORE : PORTABLE_FRONTEND_MESH_RACE);
    flow->mesh_request_course = cup->course_ids[row];
    return emit_event(flow, PORTABLE_FRONTEND_EVENT_ACCEPT, cup->course_ids[row]);
}

/* A 3D cup group: phase 0 picks a cup, phase 1 a course (GP: confirms the
 * cup). Confirm asks the platform to race the row; Select explores it. */
static int tick_mesh_course_page(PortableFrontendFlow *flow, int direction, uint16_t confirm)
{
    if (flow->mesh_request != 0u)
        return 1; /* The platform has not taken the previous request yet. */
    if (!portable_frontend_flow_mesh_cup_open(flow, flow->mesh_cup) ||
        flow->mesh_cups[flow->mesh_cup].group + 1u != flow->mesh_group)
    {
        uint8_t first;
        if (!mesh_first_cup(flow, flow->mesh_group, 0u, 1, &first) && flow->platform != 0u)
        {
            /* The platform races nothing in this mode: only Back leaves. */
            if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_B) != 0u)
                return begin_transition(flow, PORTABLE_FRONTEND_PAGE_CHARACTER,
                                        PORTABLE_FRONTEND_EVENT_BACK);
            return (flow->triggered & confirm) == 0u ||
                   emit_event(flow, PORTABLE_FRONTEND_EVENT_UNAVAILABLE, flow->mesh_cup);
        }
        if (!mesh_first_cup(flow, flow->mesh_group, 0u, 1, &first))
        {
            flow->mesh_group = 0u;
            flow->course_phase = flow->mode_cursor == 3u ? 1u : 0u;
            clamp_cup(flow);
            return 1;
        }
        flow->mesh_cup = first;
        flow->course_phase = 0u;
        flow->course_track = 0u;
    }
    if (flow->course_phase == 0u &&
        (flow->triggered & (PORTABLE_FRONTEND_BUTTON_SELECT | PORTABLE_FRONTEND_BUTTON_L |
                            PORTABLE_FRONTEND_BUTTON_R)) != 0u)
        return tick_course_track_set(flow);
    const PortableFrontendMeshCup *cup = &flow->mesh_cups[flow->mesh_cup];
    if (flow->course_phase == 0u && direction >= 0)
    {
        uint8_t open[PORTABLE_FRONTEND_MESH_CUP_CAPACITY];
        unsigned count = 0u, at = 0u;
        for (size_t index = 0u; index < flow->mesh_cup_count; ++index)
            if (flow->mesh_cups[index].group + 1u == flow->mesh_group &&
                portable_frontend_flow_mesh_cup_open(flow, index))
            {
                if (index == flow->mesh_cup)
                    at = count;
                open[count++] = (uint8_t)index;
            }
        if (count < 2u)
            return 1;
        const unsigned next = direction == 0 || direction == 2 ? (at + count - 1u) % count
                                                               : (at + 1u) % count;
        flow->mesh_cup = open[next];
        flow->course_track = 0u;
        flow->gp_preview_track = flow->gp_preview_ticks = 0u;
        return emit_event(flow, PORTABLE_FRONTEND_EVENT_MOVE, flow->mesh_cup);
    }
    if (flow->course_phase == 1u && flow->mode_cursor != 0u && (direction == 0 || direction == 1))
    {
        for (unsigned attempt = 1u; attempt < cup->course_count; ++attempt)
        {
            const unsigned row = direction == 1
                ? (flow->course_track + attempt) % cup->course_count
                : (flow->course_track + cup->course_count * 2u - attempt) % cup->course_count;
            if (mesh_row_open(flow, cup, row))
            {
                flow->course_track = (uint8_t)row;
                return emit_event(flow, PORTABLE_FRONTEND_EVENT_MOVE, row);
            }
        }
        return 1;
    }
    if (flow->course_phase == 1u && (flow->triggered & PORTABLE_FRONTEND_BUTTON_SELECT) != 0u)
        return mesh_request(flow, cup, flow->mode_cursor == 0u
            ? flow->gp_preview_track % cup->course_count : flow->course_track, 1);
    if ((flow->triggered & confirm) != 0u)
    {
        if (flow->course_phase == 0u)
        {
            flow->course_phase = 1u;
            flow->course_track = mesh_first_row(flow, cup);
            flow->gp_preview_track = flow->gp_preview_ticks = 0u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_OPEN, flow->mesh_cup);
        }
        return mesh_request(flow, cup, flow->mode_cursor == 0u ? 0u : flow->course_track, 0);
    }
    if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_B) != 0u)
    {
        if (flow->course_phase != 0u)
        {
            flow->course_phase = 0u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_BACK, flow->course_phase);
        }
        return begin_transition(flow, PORTABLE_FRONTEND_PAGE_CHARACTER,
                                PORTABLE_FRONTEND_EVENT_BACK);
    }
    return flow->event_overflow == 0u;
}

static int tick_course_page(PortableFrontendFlow *flow, int direction, uint16_t confirm)
{
    if (flow->mode_cursor != 3u && (flow->course_cup == PORTABLE_FRONTEND_ARENA_CUP ||
                                    flow->course_cup >= PORTABLE_FRONTEND_COURSE_CUPS))
        flow->course_cup = 0u;
    if (flow->page == PORTABLE_FRONTEND_PAGE_RECORDS && flow->course_phase >= 2u)
        return tick_records_page(flow, direction, confirm);
    if (flow->page == PORTABLE_FRONTEND_PAGE_COURSE && flow->mesh_group != 0u)
        return tick_mesh_course_page(flow, direction, confirm);
    if (flow->course_phase == 2u && flow->mode_cursor == 1u &&
        (flow->ghost_choices & (flow->ghost_choices - 1u)) != 0u &&
        (direction == 0 || direction == 1))
    {
        const unsigned step = direction == 1 ? 1u : 2u;
        do
        {
            flow->ghost_choice = (uint8_t)((flow->ghost_choice + step) % 3u);
        } while ((flow->ghost_choices & (1u << flow->ghost_choice)) == 0u);
        return emit_event(flow, PORTABLE_FRONTEND_EVENT_MOVE, flow->ghost_choice);
    }
    /* B89C: L/R (Select kept as an alias) swaps track sets; locked is silent.
     * Sets cycle GBA -> Extra -> Custom, skipping empty or locked ones. */
    if ((flow->course_phase == 0u ||
         (flow->mode_cursor == 3u && flow->course_phase == 1u && flow->mesh_group_count != 0u &&
          flow->page == PORTABLE_FRONTEND_PAGE_COURSE)) &&
        (flow->triggered & (PORTABLE_FRONTEND_BUTTON_SELECT | PORTABLE_FRONTEND_BUTTON_L |
                            PORTABLE_FRONTEND_BUTTON_R)) != 0u)
        return tick_course_track_set(flow);
    const uint8_t old_cup = flow->course_cup;
    if (flow->course_phase == 0u && direction >= 0)
    {
        const uint8_t allowed = portable_frontend_flow_cup_access(flow);
        if (allowed == 0u)
            return 0;
        const uint8_t step = (direction == 0 || direction == 2) ? 4u : 1u;
        const uint8_t offset =
            portable_track_set_first_cup(portable_track_set_of_cup(flow->course_cup));
        do
        {
            flow->course_cup =
                (uint8_t)(offset + (flow->course_cup - offset + step) % PORTABLE_FRONTEND_CUPS);
        } while (!(allowed & (1u << (flow->course_cup - offset))));
    }
    else if (flow->course_phase == 1u && flow->mode_cursor != 0u && direction == 0)
        flow->course_track = (uint8_t)((flow->course_track + 3u) % 4u);
    else if (flow->course_phase == 1u && flow->mode_cursor != 0u && direction == 1)
        flow->course_track = (uint8_t)((flow->course_track + 1u) % 4u);
    else
        direction = -1;
    if (direction >= 0 && !emit_event(flow, PORTABLE_FRONTEND_EVENT_MOVE,
                                      (uint32_t)flow->course_cup * 4u + flow->course_track))
        return 0;
    if (flow->course_cup != old_cup)
    {
        flow->gp_preview_track = 0u;
        return 1; /* A new cup wins over a same-turn confirm. */
    }
    if ((flow->triggered & confirm) != 0u)
    {
        if (mesh_only_selected(flow))
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_UNAVAILABLE,
                              (uint32_t)flow->course_cup * 4u + flow->course_track);
        if (!(portable_frontend_flow_cup_access(flow) &
              (1u << (flow->course_cup -
                      portable_track_set_first_cup(portable_track_set_of_cup(flow->course_cup))))))
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_UNAVAILABLE, flow->course_cup * 4u);
        const PortableFrontendCourseSlot *slot =
            course_slot(flow, flow->course_cup, flow->course_track);
        if (flow->course_phase == 0u)
        {
            flow->course_track = 0u;
            slot = course_slot(flow, flow->course_cup, 0u);
            if (flow->mode_cursor == 0u)
            {
                if (slot == NULL || slot->available == 0u)
                    return emit_event(flow, PORTABLE_FRONTEND_EVENT_UNAVAILABLE,
                                      (uint32_t)flow->course_cup * 4u);
                flow->course_phase = 1u;
                flow->cup_motion_phase = 1u;
                flow->cup_motion_tick = 0u;
                return emit_event(flow, PORTABLE_FRONTEND_EVENT_OPEN, flow->course_cup);
            }
            flow->course_phase = 1u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_OPEN, flow->course_cup);
        }
        if (flow->mode_cursor == 0u)
        {
            /* The cycling cover is a preview, never the GP starting row. */
            slot = course_slot(flow, flow->course_cup, 0u);
            if (slot == NULL || !slot->available)
                return 0;
            flow->course_track = 0u;
            flow->selected_course = slot->content_id;
            if (!begin_transition(flow, PORTABLE_FRONTEND_PAGE_RACE,
                                  PORTABLE_FRONTEND_EVENT_ACCEPT))
                return 0;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_RACE_LAUNCH, slot->content_id);
        }
        if (slot == NULL || slot->available == 0u)
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_UNAVAILABLE,
                              (uint32_t)flow->course_cup * 4u + flow->course_track);
        flow->selected_course = slot->content_id;
        if (flow->course_phase == 1u)
        {
            flow->course_phase = 2u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_OPEN, slot->content_id);
        }
        if (!begin_transition(flow, PORTABLE_FRONTEND_PAGE_RACE, PORTABLE_FRONTEND_EVENT_ACCEPT))
            return 0;
        return emit_event(flow, PORTABLE_FRONTEND_EVENT_RACE_LAUNCH, slot->content_id);
    }
    if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_B) != 0u)
    {
        if (flow->mode_cursor == 0u && flow->course_phase == 1u)
        {
            flow->course_phase = 0u;
            flow->cup_motion_phase = 2u;
            flow->cup_motion_tick = 0u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_BACK, PORTABLE_FRONTEND_PAGE_COURSE);
        }
        if (flow->mode_cursor == 3u && flow->course_phase == 1u)
            return begin_transition(flow, PORTABLE_FRONTEND_PAGE_CHARACTER,
                                    PORTABLE_FRONTEND_EVENT_BACK);
        if (flow->course_phase != 0u)
        {
            --flow->course_phase;
            portable_frontend_flow_set_ghost_choices(flow, 0u);
            flow->selected_course = 0u;
            return emit_event(flow, PORTABLE_FRONTEND_EVENT_BACK, flow->course_phase);
        }
        return begin_transition(flow, PORTABLE_FRONTEND_PAGE_CHARACTER,
                                PORTABLE_FRONTEND_EVENT_BACK);
    }
    return flow->event_overflow == 0u;
}

static int tick_race_page(PortableFrontendFlow *flow)
{
    if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_START) != 0u)
    {
        if (!emit_event(flow, PORTABLE_FRONTEND_EVENT_PAUSE, 0u))
            return 0;
        return enter_page(flow, PORTABLE_FRONTEND_PAGE_PAUSE);
    }
    return flow->event_overflow == 0u;
}

static int tick_pause_page(PortableFrontendFlow *flow, uint16_t buttons)
{
    switch (portable_pause_menu_tick(&flow->pause_menu, buttons))
    {
    case PORTABLE_PAUSE_ACTION_CONTINUE:
        if (!emit_event(flow, PORTABLE_FRONTEND_EVENT_RESUME, 0u))
            return 0;
        return enter_page(flow, PORTABLE_FRONTEND_PAGE_RACE);
    case PORTABLE_PAUSE_ACTION_RETRY:
        if (!emit_event(flow, PORTABLE_FRONTEND_EVENT_RACE_RESTART, flow->selected_course))
            return 0;
        return enter_page(flow, PORTABLE_FRONTEND_PAGE_RACE);
    case PORTABLE_PAUSE_ACTION_CHANGE_TRACK:
        return portable_frontend_flow_cancel_race_to(flow, PORTABLE_FRONTEND_PAGE_COURSE);
    case PORTABLE_PAUSE_ACTION_CHANGE_CHARACTER:
        return portable_frontend_flow_cancel_race_to(flow, PORTABLE_FRONTEND_PAGE_CHARACTER);
    case PORTABLE_PAUSE_ACTION_QUIT:
        return portable_frontend_flow_cancel_race_to(flow, PORTABLE_FRONTEND_PAGE_MODE);
    default:
        break;
    }
    return flow->event_overflow == 0u;
}

static int tick_results_page(PortableFrontendFlow *flow, uint16_t confirm)
{
    if ((flow->triggered & confirm) != 0u)
    {
        flow->result_ticks = 0u;
        if (!begin_transition(flow, PORTABLE_FRONTEND_PAGE_RACE,
                              PORTABLE_FRONTEND_EVENT_RACE_RESTART))
            return 0;
        return emit_event(flow, PORTABLE_FRONTEND_EVENT_RACE_LAUNCH, flow->selected_course);
    }
    if ((flow->triggered & PORTABLE_FRONTEND_BUTTON_B) != 0u)
    {
        flow->result_ticks = 0u;
        return begin_transition(flow, PORTABLE_FRONTEND_PAGE_COURSE, PORTABLE_FRONTEND_EVENT_BACK);
    }
    return flow->event_overflow == 0u;
}

static int tick_frontend_input(PortableFrontendFlow *flow, uint16_t buttons)
{
    const uint16_t confirm = PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START;
    int direction;
    if (flow == NULL || flow->page < PORTABLE_FRONTEND_PAGE_TITLE_INTRO ||
        flow->page > PORTABLE_FRONTEND_PAGE_RECORDS || flow->event_overflow != 0u ||
        !update_input(flow, buttons))
        return 0;
    ++flow->tick;
    tick_title_ambient(flow);
    if (flow->transition_ticks != 0u)
    {
        if (flow->short_transitions)
            flow->transition_ticks = 1u;
        --flow->transition_ticks;
        if (flow->transition_ticks == 0u && !enter_page(flow, flow->pending_page))
            return 0;
        return 1;
    }
    ++flow->page_ticks;
    if (flow->page == PORTABLE_FRONTEND_PAGE_COURSE && flow->cup_motion_phase != 0u)
    {
        if (flow->short_transitions || ++flow->cup_motion_tick >= 17u)
        {
            flow->cup_motion_phase = flow->cup_motion_tick = 0u;
        }
        return 1; /* AF94 lock/unlock terminal turn still consumes input. */
    }
    direction = direction_index(flow->repeated);
    switch (flow->page)
    {
    case PORTABLE_FRONTEND_PAGE_TITLE_INTRO:
        return tick_title_intro_page(flow, confirm);
    case PORTABLE_FRONTEND_PAGE_TITLE_MENU:
        return tick_title_menu_page(flow, direction, confirm);
    case PORTABLE_FRONTEND_PAGE_MODE:
        return tick_mode_page(flow, direction, confirm);
    case PORTABLE_FRONTEND_PAGE_TIME_TRIAL:
        return tick_time_trial_page(flow, direction, confirm);
    case PORTABLE_FRONTEND_PAGE_CHARACTER:
        return tick_character_page(flow, direction);
    case PORTABLE_FRONTEND_PAGE_RECORDS:
    case PORTABLE_FRONTEND_PAGE_COURSE:
        return tick_course_page(flow, direction, confirm);
    case PORTABLE_FRONTEND_PAGE_RACE:
        return tick_race_page(flow);
    case PORTABLE_FRONTEND_PAGE_PAUSE:
        return tick_pause_page(flow, buttons);
    case PORTABLE_FRONTEND_PAGE_RESULTS:
        return tick_results_page(flow, confirm);
    default:
        return 0;
    }
}

int portable_frontend_flow_set_title_cursor_sine(PortableFrontendFlow *flow,
                                                 const int32_t samples[8])
{
    if (flow == NULL)
        return 0;
    if (samples != NULL)
        for (unsigned i = 0u; i < 8u; ++i)
            if (samples[i] < 0 || samples[i] > 32768)
                return 0;
    PortableFrontendTitleCursor *motion = &flow->title_cursor_motion;
    if (samples != NULL)
        memcpy(motion->sine_q15, samples, sizeof motion->sine_q15);
    motion->configured = (uint8_t)(samples != NULL);
    motion->y = motion->start_y = (int16_t)(132 - 16 * (int)flow->title_cursor);
    motion->phase = motion->ticks = 0u;
    return 1;
}

static void tick_title_cursor(PortableFrontendFlow *flow)
{
    if (flow->page != PORTABLE_FRONTEND_PAGE_TITLE_MENU || flow->player_count_open)
        return;
    PortableFrontendTitleCursor *motion = &flow->title_cursor_motion;
    const int target = 132 - 16 * (int)flow->title_cursor;
    if (!motion->configured || flow->short_transitions)
    {
        motion->y = (int16_t)target;
        motion->phase = motion->ticks = 0u;
        return;
    }
    if (motion->phase == 1u)
    {
        motion->start_y = motion->y;
        motion->phase = 2u;
        motion->ticks = 0u;
    }
    else if (motion->phase == 2u)
    {
        if (++motion->ticks <= 8u)
        {
            const int32_t product =
                (target - motion->start_y) * motion->sine_q15[motion->ticks - 1u];
            const int offset = product >= 0 ? product / 32768 : -((-product + 32767) / 32768);
            motion->y = (int16_t)(motion->start_y + offset);
        }
        else
        {
            motion->y = (int16_t)target;
            motion->phase = motion->ticks = 0u;
        }
    }
}

static int heading_group(PortableFrontendPage page, uint8_t mode, uint8_t course_phase)
{
    if (page == PORTABLE_FRONTEND_PAGE_MODE || page == PORTABLE_FRONTEND_PAGE_TIME_TRIAL)
        return 0;
    if (page == PORTABLE_FRONTEND_PAGE_CHARACTER)
        return 1;
    if (page == PORTABLE_FRONTEND_PAGE_COURSE || page == PORTABLE_FRONTEND_PAGE_RECORDS)
        return mode != 0u && course_phase != 0u ? 3 : 2;
    return -1;
}
int portable_frontend_flow_set_heading_rules(PortableFrontendFlow *flow,
                                             const PortableMenuHeadingRules *rules,
                                             const int32_t sine[1024])
{
    if (!flow)
        return 0;
    flow->heading_configured = flow->heading_static_mask = 0u;
    memset(&flow->heading, 0, sizeof flow->heading);
    memset(&flow->medallion, 0, sizeof flow->medallion);
    if (!rules)
        return 1;
    if (!sine)
        return 0;
    for (unsigned i = 0; i < 4u; ++i)
    {
        if (!rules->groups[i].count || rules->groups[i].count > 16u)
            return 0;
        for (unsigned j = 0; j < rules->groups[i].count; ++j)
        {
            const PortableMenuHeadingLetter *r = &rules->groups[i].letters[j];
            if (r->x < -1024 || r->x > 1024 || r->y < -1024 || r->y > 1024)
                return 0;
        }
    }
    for (unsigned i = 0; i < 1024u; ++i)
        if (sine[i] < -32768 || sine[i] > 32768)
            return 0;
    flow->heading_rules = *rules;
    memcpy(flow->heading_sine, sine, sizeof flow->heading_sine);
    flow->heading_configured = 1u;
    const int group = heading_group(flow->page, flow->mode_cursor, flow->course_phase);
    return group < 0 || portable_menu_heading_start(&flow->heading, rules, (unsigned)group, 1);
}
static int tick_heading(PortableFrontendFlow *flow)
{
    if (!flow->heading_configured)
        return 1;
    int group = heading_group(flow->page, flow->mode_cursor, flow->course_phase), outgoing = 0,
        exit_now = 0;
    if (flow->transition_ticks)
    {
        const unsigned age = flow->transition_total_ticks - flow->transition_ticks;
        const int pending = heading_group(flow->pending_page, flow->mode_cursor,
                                          flow->pending_page == PORTABLE_FRONTEND_PAGE_RECORDS ? 2u
                                          : flow->pending_page == PORTABLE_FRONTEND_PAGE_COURSE
                                              ? (uint8_t)(flow->mode_cursor == 3u)
                                              : flow->course_phase);
        if (age < flow->transition_out_ticks)
        {
            outgoing = group != pending;
            exit_now =
                outgoing && !(flow->page == PORTABLE_FRONTEND_PAGE_CHARACTER &&
                              flow->pending_page == PORTABLE_FRONTEND_PAGE_COURSE && age < 33u);
        }
        else
        {
            /* 3FD0 runs after the page's loading/controller terminal turns.
             * Title's player medallion precedes the mode header constructor. */
            const unsigned delay = flow->page == PORTABLE_FRONTEND_PAGE_TITLE_MENU &&
                                           flow->pending_page == PORTABLE_FRONTEND_PAGE_MODE
                                       ? 33u
                                       : 3u;
            group = pending == group                           ? pending
                    : age < flow->transition_out_ticks + delay ? -1
                                                               : pending;
        }
    }
    if (group < 0)
    {
        flow->heading.initialized = 0u;
        return 1;
    }
    if (!flow->heading.initialized || flow->heading.group != (unsigned)group ||
        (!outgoing && flow->heading.exiting))
    {
        if (!portable_menu_heading_start(&flow->heading, &flow->heading_rules, (unsigned)group,
                                         flow->short_transitions))
            return 0;
    }
    if (exit_now)
        portable_menu_heading_exit(&flow->heading);
    if (flow->short_transitions)
        return portable_menu_heading_start(&flow->heading, &flow->heading_rules, (unsigned)group,
                                           1);
    return portable_menu_heading_tick(&flow->heading, flow->heading_sine);
}

static int medallion_selection(const PortableFrontendFlow *flow, unsigned *group, unsigned *cursor)
{
    if (flow->page == PORTABLE_FRONTEND_PAGE_TIME_TRIAL)
    {
        *group = 2u;
        *cursor = flow->time_trial_cursor;
        return 1;
    }
    if (flow->page != PORTABLE_FRONTEND_PAGE_MODE)
        return 0;
    if (flow->mode_phase)
    {
        *group = 1u;
        *cursor = flow->race_speed;
    }
    else if (flow->players > 1u)
    {
        *group = 3u;
        *cursor = flow->mode_cursor == 3u ? 1u : 0u;
    }
    else
    {
        *group = 0u;
        *cursor = flow->mode_cursor;
    }
    return 1;
}
int portable_frontend_flow_tick(PortableFrontendFlow *flow, uint16_t buttons)
{
    if (!flow || !valid_buttons(buttons) || flow->page < PORTABLE_FRONTEND_PAGE_TITLE_INTRO ||
        flow->page > PORTABLE_FRONTEND_PAGE_RECORDS || flow->event_overflow)
        return 0;
    unsigned medallion_group = 0u, medallion_cursor = 0u;
    const int medallion_active = flow->heading_configured && !flow->transition_ticks &&
                                 medallion_selection(flow, &medallion_group, &medallion_cursor);
    if (medallion_active && !portable_menu_medallion_tick(&flow->medallion, medallion_group,
                                                          medallion_cursor, flow->heading_sine))
        return 0;
    if (!tick_frontend_input(flow, buttons))
        return 0;
    if (medallion_active && !flow->transition_ticks)
    {
        unsigned group, cursor;
        if (medallion_selection(flow, &group, &cursor) && group == medallion_group)
            portable_menu_medallion_retarget(&flow->medallion, cursor);
    }
    /* Source title_8000350 follows input and can restart from the prior
     * rendered position on the very turn the direction changes. */
    tick_title_cursor(flow);
    if (!tick_heading(flow))
        return 0;
    if (flow->page == PORTABLE_FRONTEND_PAGE_COURSE && flow->mode_cursor == 0u)
    {
        /* AF94 C44x advances the displayed cover after65 idle-owner turns;
         * it does not change the GP route selected by the second A edge. */
        if (++flow->gp_preview_ticks > 64u)
        {
            flow->gp_preview_ticks = 0u;
            flow->gp_preview_track = (uint8_t)((flow->gp_preview_track + 1u) & 3u);
        }
    }
    return 1;
}

int portable_frontend_flow_finish_race(PortableFrontendFlow *flow, uint32_t result_ticks)
{
    if (flow == NULL || flow->page != PORTABLE_FRONTEND_PAGE_RACE || flow->transition_ticks != 0u ||
        result_ticks == 0u || flow->event_overflow != 0u)
        return 0;
    flow->result_ticks = result_ticks;
    if (!emit_event(flow, PORTABLE_FRONTEND_EVENT_RACE_FINISH, result_ticks))
        return 0;
    return begin_transition(flow, PORTABLE_FRONTEND_PAGE_RESULTS, PORTABLE_FRONTEND_EVENT_ACCEPT);
}

int portable_frontend_flow_cancel_race(PortableFrontendFlow *flow)
{
    return portable_frontend_flow_cancel_race_to(flow, PORTABLE_FRONTEND_PAGE_COURSE);
}

int portable_frontend_flow_cancel_race_to(PortableFrontendFlow *flow,
                                          PortableFrontendPage destination)
{
    if (flow == NULL ||
        (flow->page != PORTABLE_FRONTEND_PAGE_RACE && flow->page != PORTABLE_FRONTEND_PAGE_PAUSE) ||
        flow->transition_ticks != 0u ||
        (destination != PORTABLE_FRONTEND_PAGE_COURSE &&
         destination != PORTABLE_FRONTEND_PAGE_CHARACTER &&
         destination != PORTABLE_FRONTEND_PAGE_MODE))
        return 0;
    if (!emit_event(flow, PORTABLE_FRONTEND_EVENT_RACE_CANCEL, destination))
        return 0;
    if (!enter_page(flow, destination))
        return 0;
    if (destination == PORTABLE_FRONTEND_PAGE_COURSE)
        flow->course_phase = flow->mode_cursor == 0u ? 0u : 1u;
    portable_frontend_flow_set_ghost_choices(flow, 0u);
    if (destination == PORTABLE_FRONTEND_PAGE_MODE)
        flow->mode_phase = 0u;
    return 1;
}

int portable_frontend_flow_replay(PortableFrontendFlow *flow,
                                  const PortableFrontendReplayFrame *frames, size_t frame_count)
{
    size_t index;
    if (flow == NULL || (frames == NULL && frame_count != 0u))
        return 0;
    for (index = 0u; index < frame_count; ++index)
    {
        if (!portable_frontend_flow_tick(flow, frames[index].buttons))
            return 0;
        if (frames[index].command == PORTABLE_FRONTEND_REPLAY_FINISH)
        {
            if (!portable_frontend_flow_finish_race(flow, frames[index].value))
                return 0;
        }
        else if (frames[index].command != PORTABLE_FRONTEND_REPLAY_NONE)
            return 0;
    }
    return 1;
}

static uint32_t hash_u32(uint32_t hash, uint32_t value)
{
    unsigned shift;
    for (shift = 0u; shift < 32u; shift += 8u)
    {
        hash ^= (uint8_t)(value >> shift);
        hash *= UINT32_C(16777619);
    }
    return hash;
}

uint32_t portable_frontend_flow_checksum(const PortableFrontendFlow *flow)
{
    uint32_t hash = UINT32_C(2166136261);
    size_t index;
    if (flow == NULL)
        return 0u;
    hash = hash_u32(hash, (uint32_t)flow->page);
    hash = hash_u32(hash, (uint32_t)flow->pending_page);
    hash = hash_u32(hash, flow->tick);
    hash = hash_u32(hash, flow->page_ticks);
    hash = hash_u32(hash, flow->buttons);
    hash = hash_u32(hash, flow->triggered);
    hash = hash_u32(hash, flow->repeated);
    hash = hash_u32(hash, flow->hold_ticks);
    hash = hash_u32(hash, flow->transition_ticks);
    hash = hash_u32(hash, flow->transition_total_ticks);
    hash = hash_u32(hash, flow->transition_out_ticks);
    hash = hash_u32(hash, flow->transition_from_mode_phase);
    hash = hash_u32(hash, flow->title_cursor);
    /* Presentation-only title actors stay outside the v1/v2 navigation CRC. */
    if (flow->heading_configured)
    {
        hash = hash_u32(hash, UINT32_C(0x3147484d));
        hash = hash_u32(hash, flow->heading_static_mask);
        hash = hash_u32(hash, flow->heading.initialized);
        hash = hash_u32(hash, flow->heading.group);
        hash = hash_u32(hash, flow->heading.count);
        hash = hash_u32(hash, flow->heading.exiting);
        for (unsigned i = 0; i < flow->heading.count && i < 16u; ++i)
        {
            const PortableMenuHeadingCell *c = &flow->heading.cells[i];
            hash = hash_u32(hash, (uint16_t)c->x);
            hash = hash_u32(hash, (uint16_t)c->y);
            hash = hash_u32(hash, (uint16_t)c->target_x);
            hash = hash_u32(hash, (uint16_t)c->target_y);
            hash = hash_u32(hash, (uint16_t)c->origin_y);
            hash = hash_u32(hash, (uint16_t)c->inverse_x);
            hash = hash_u32(hash, (uint16_t)c->inverse_y);
            hash = hash_u32(hash, c->mode);
            hash = hash_u32(hash, c->ticks);
        }
        hash = hash_u32(hash, flow->medallion.initialized);
        hash = hash_u32(hash, flow->medallion.group);
        hash = hash_u32(hash, flow->medallion.cursor);
        hash = hash_u32(hash, flow->medallion.grow_tick);
        hash = hash_u32(hash, flow->medallion.pulse);
        for (unsigned i = 0; i < 4u; ++i)
            hash = hash_u32(hash, flow->medallion.inverse[i]);
    }
    if (flow->mesh_group != 0u || flow->mesh_request != 0u)
    {
        hash = hash_u32(hash, UINT32_C(0x4d455348));
        hash = hash_u32(hash, flow->mesh_group);
        hash = hash_u32(hash, flow->mesh_cup);
        hash = hash_u32(hash, flow->mesh_request);
        hash = hash_u32(hash, flow->mesh_request_course);
    }
    hash = hash_u32(hash, flow->mode_cursor);
    hash = hash_u32(hash, flow->mode_phase);
    hash = hash_u32(hash, flow->race_speed);
    if (flow->mode_cursor == 2u)
    {
        hash = hash_u32(hash, flow->quick_options_open);
        hash = hash_u32(hash, flow->quick_options_cursor);
        hash = hash_u32(hash, flow->ghost_display_off);
        hash = hash_u32(hash, flow->records_top);
        hash = hash_u32(hash, flow->quick_run_options.five_laps);
        hash = hash_u32(hash, flow->quick_run_options.items_off);
        hash = hash_u32(hash, flow->quick_run_options.coins_off);
    }
    if (flow->players > 1u || flow->player_count_open)
    {
        hash = hash_u32(hash, flow->players);
        hash = hash_u32(hash, flow->player_count_open);
        hash = hash_u32(hash, flow->character_player);
        for (index = 0u; index < PORTABLE_FRONTEND_MAX_PLAYERS; ++index)
            hash = hash_u32(hash, flow->player_characters[index]);
        if (flow->quick_options_open || flow->vs_options.five_laps || flow->vs_options.items_off ||
            flow->vs_options.coins_off || flow->vs_options.mushroom_start)
        {
            hash = hash_u32(hash, flow->quick_options_open);
            hash = hash_u32(hash, flow->quick_options_cursor);
            hash = hash_u32(hash, flow->vs_options.five_laps);
            hash = hash_u32(hash, flow->vs_options.items_off);
            hash = hash_u32(hash, flow->vs_options.mushroom_start);
            hash = hash_u32(hash, flow->vs_options.coins_off);
        }
    }
    if (flow->kart_count)
    {
        hash = hash_u32(hash, flow->kart_choice);
        hash = hash_u32(hash, flow->kart_cursor);
        hash = hash_u32(hash, flow->selected_kart);
    }
    hash = hash_u32(hash, flow->time_trial_cursor);
    hash = hash_u32(hash, flow->character_cursor);
    if (flow->character_page != 0u)
        hash = hash_u32(hash, flow->character_page);
    if (flow->platform != 0u)
        hash = hash_u32(hash, UINT32_C(0x504c4154) ^ flow->platform);
    hash = hash_u32(hash, flow->character_locked);
    hash = hash_u32(hash, flow->course_cup);
    hash = hash_u32(hash, flow->course_track);
    hash = hash_u32(hash, flow->course_phase);
    if (flow->page == PORTABLE_FRONTEND_PAGE_RECORDS)
        hash = hash_u32(hash, flow->record_erase_choice);
    if (flow->record_ghost_slot != 0u)
        hash = hash_u32(hash, flow->record_ghost_slot);
    if (flow->ghost_choices != 0u)
    {
        hash = hash_u32(hash, flow->ghost_choices);
        hash = hash_u32(hash, flow->ghost_choice);
    }
    hash = hash_u32(hash, flow->paused);
    hash = hash_u32(hash, flow->pause_menu.buttons);
    hash = hash_u32(hash, flow->pause_menu.cursor);
    hash = hash_u32(hash, flow->pause_menu.open_ticks);
    hash = hash_u32(hash, flow->selected_character);
    hash = hash_u32(hash, flow->selected_course);
    hash = hash_u32(hash, flow->result_ticks);
    hash = hash_u32(hash, (uint32_t)flow->event_count);
    for (index = 0u; index < flow->event_count; ++index)
    {
        hash = hash_u32(hash, flow->events[index].tick);
        hash = hash_u32(hash, flow->events[index].value);
        hash = hash_u32(hash, flow->events[index].type);
        hash = hash_u32(hash, flow->events[index].page);
    }
    return hash;
}
