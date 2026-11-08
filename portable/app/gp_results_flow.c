#include "gp_results_flow.h"

#include <stddef.h>
#include <string.h>

int portable_gp_results_advance_allowed(uint32_t result_ticks,uint8_t short_transitions)
{
    return short_transitions!=0u || result_ticks>UINT32_C(0x21c);
}

void portable_gp_results_flow_init(PortableGpResultsFlow *flow)
{
    if (flow != NULL)
        memset(flow, 0, sizeof *flow);
}

PortableGpResultsStatus portable_gp_results_flow_tick_entry(
    PortableGpResultsFlow *flow, uint8_t cup_record_changed,
    uint8_t course_record_changed)
{
    if (flow == NULL)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    flow->background_copy_pending = 0u;
    flow->background_clear_pending = 0u;
    ++flow->total_ticks;
    if (flow->state == 0u)
    {
        ++flow->state_counter;
        if (flow->state_counter > UINT32_C(0x00b4))
        {
            flow->records_changed = (uint8_t)(
                cup_record_changed != 0u || course_record_changed != 0u);
            flow->state = 1u;
            flow->state_counter = 0u;
        }
        return PORTABLE_GP_RESULTS_OK;
    }
    if (flow->state == 1u)
    {
        flow->background_copy_pending = 1u;
        flow->background_clear_pending = 1u;
        flow->state = 2u;
        return PORTABLE_GP_RESULTS_OK;
    }
    return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
}

PortableGpResultsStatus portable_gp_results_flow_tick_reveal(
    PortableGpResultsFlow *flow, uint8_t course_in_cup)
{
    uint8_t row_count;
    uint8_t row_start;
    uint8_t row_step;
    uint8_t x;
    if (flow == NULL)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    if (flow->state != 2u)
        return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    if (course_in_cup > 5u)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    flow->background_copy_pending = 0u;
    flow->background_clear_pending = 0u;
    flow->reveal_pending = 0u;
    ++flow->total_ticks;
    ++flow->state_counter;
    if (flow->state_counter <= 1u)
        return PORTABLE_GP_RESULTS_OK;
    flow->state_counter = 0u;
    if (flow->reveal_progress > 7u)
    {
        flow->state = 3u;
        flow->reveal_progress = 0u;
        flow->row_count = 0u;
        return PORTABLE_GP_RESULTS_OK;
    }
    ++flow->reveal_progress;
    flow->background_clear_pending = 1u;
    flow->reveal_pending = 1u;
    /* 801E248: three laps use two-row cells; more pack one row each. */
    row_start = course_in_cup > 3u ? 2u : 1u;
    row_step = course_in_cup > 3u ? 1u : 2u;
    row_count = (uint8_t)(((uint32_t)(course_in_cup + 1u) *
                           flow->reveal_progress) >> 3u);
    x = (uint8_t)(18u - (((uint32_t)9u * flow->reveal_progress) >> 3u));
    flow->row_count = row_count;
    for (uint8_t index = 0u; index < row_count; ++index)
    {
        flow->rows[index].racer_index =
            index < course_in_cup ? index : course_in_cup;
        flow->rows[index].x = (uint8_t)(
            x - (uint8_t)(course_in_cup <= index));
        flow->rows[index].y = (uint8_t)(row_start + row_step * index);
    }
    flow->header_x = x;
    flow->header_y = flow->reveal_progress;
    flow->header_variant = (uint8_t)(flow->reveal_progress > 7u);
    return PORTABLE_GP_RESULTS_OK;
}

PortableGpResultsStatus portable_gp_results_flow_tick_lap_hold(PortableGpResultsFlow *flow)
{
    if(flow==NULL)return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    if(flow->state!=3u)return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    ++flow->total_ticks;
    flow->background_copy_pending=1u;
    flow->background_clear_pending=1u;
    flow->song_pending=0u;
    if(flow->total_ticks>UINT32_C(0x021c))flow->state=4u;
    return PORTABLE_GP_RESULTS_OK;
}

PortableGpResultsStatus portable_gp_results_flow_tick_hold(
    PortableGpResultsFlow *flow, uint16_t race_mode_flags)
{
    if (flow == NULL)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    if (flow->state != 3u)
        return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    flow->background_copy_pending = 0u;
    flow->background_clear_pending = 0u;
    flow->song_pending = 0u;
    flow->exit_pending = 0u;
    ++flow->total_ticks;
    if (flow->total_ticks <= UINT32_C(0x021c))
        return PORTABLE_GP_RESULTS_OK;
    if ((race_mode_flags & UINT16_C(0x4000)) != 0u)
    {
        flow->background_clear_pending = 1u;
        flow->exit_pending = 1u;
        flow->exit_code = 9u;
        return PORTABLE_GP_RESULTS_OK;
    }
    flow->song_pending = 1u;
    flow->song_id = flow->records_changed ? UINT16_C(14) : UINT16_C(15);
    flow->result_delay = 15u;
    flow->background_copy_pending = 1u;
    flow->state = 4u;
    return PORTABLE_GP_RESULTS_OK;
}

PortableGpResultsStatus portable_gp_results_flow_tick_exit_wipe(
    PortableGpResultsFlow *flow, uint8_t advance_triggered)
{
    static const uint8_t source_y[PORTABLE_GP_RESULTS_EXIT_BAND_COUNT] = {
        1u, 3u, 5u, 7u, 8u};
    static const uint8_t row_count[PORTABLE_GP_RESULTS_EXIT_BAND_COUNT] = {
        2u, 2u, 2u, 1u, 2u};
    static const uint8_t start_offset[PORTABLE_GP_RESULTS_EXIT_BAND_COUNT] = {
        0u, 3u, 6u, 9u, 12u};
    uint8_t band_count = 0u;
    if (flow == NULL)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    if (flow->state != 4u)
        return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    flow->background_copy_pending = 0u;
    flow->background_clear_pending = 0u;
    flow->song_pending = 0u;
    flow->exit_wipe_pending = 0u;
    flow->exit_band_count = 0u;
    flow->advance_skip_pending = 0u;
    ++flow->total_ticks;
    ++flow->state_counter;
    if (flow->state_counter > 0u)
    {
        flow->state_counter = 0u;
        ++flow->exit_wipe_progress;
        if (flow->exit_wipe_progress > 43u)
        {
            flow->state = 5u;
            flow->exit_wipe_progress = 0u;
        }
        else
        {
            for (uint8_t index = 0u;
                 index < PORTABLE_GP_RESULTS_EXIT_BAND_COUNT; ++index)
            {
                if (flow->exit_wipe_progress <= start_offset[index])
                    continue;
                flow->exit_bands[band_count].source_y = source_y[index];
                flow->exit_bands[band_count].row_count = row_count[index];
                flow->exit_bands[band_count].destination_x = (int8_t)(
                    (int)start_offset[index] -
                    (int)flow->exit_wipe_progress);
                ++band_count;
            }
            flow->exit_band_count = band_count;
            flow->exit_wipe_pending = 1u;
        }
    }
    if (advance_triggered != 0u)
    {
        flow->state = 9u;
        flow->advance_skip_pending = 1u;
    }
    return PORTABLE_GP_RESULTS_OK;
}

PortableGpResultsStatus portable_gp_results_flow_tick_followup_setup(
    PortableGpResultsFlow *flow)
{
    if (flow == NULL)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    if (flow->state != 5u)
        return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    ++flow->total_ticks;
    flow->followup_graphics_pending = 1u;
    flow->followup_map_pending = 1u;
    flow->scroll_map_pending = 1u;
    flow->state = 6u;
    return PORTABLE_GP_RESULTS_OK;
}

PortableGpResultsStatus portable_gp_results_flow_tick_record_setup(
    PortableGpResultsFlow *flow,
    const PortableGpResultsRecord records[PORTABLE_GP_RESULTS_RECORD_ROW_COUNT],
    const uint8_t record_identities[8])
{
    if (flow == NULL || records == NULL)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    if (flow->state != 6u)
        return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    for(unsigned i=0u;i<PORTABLE_GP_RESULTS_RECORD_ROW_COUNT;++i)
        if(!(records[i].flags & UINT32_C(0x10)) &&
           (record_identities==NULL || record_identities[records[i].flags&7u]>7u))
            return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    ++flow->total_ticks;
    flow->track_name_pending = 1u;
    flow->followup_object_pending = 1u;
    flow->record_rows_pending = 1u;
    flow->record_row_count = PORTABLE_GP_RESULTS_RECORD_ROW_COUNT;
    for (uint8_t index = 0u;
         index < PORTABLE_GP_RESULTS_RECORD_ROW_COUNT; ++index)
    {
        const uint32_t flags = records[index].flags;
        flow->record_rows[index].display_row = index < 5u ? index : 6u;
        flow->record_rows[index].identity = (flags & UINT32_C(0x10)) != 0u ?
            9u : record_identities[flags & UINT32_C(7)];
        flow->record_rows[index].time_centiseconds =
            records[index].time_centiseconds;
        flow->record_rows[index].emphasized =
            (uint8_t)(records[index].emphasized != 0u);
        flow->record_rows[index].roster_driver_id=records[index].roster_driver_id;
    }
    flow->scroll_x = UINT16_C(0x016c);
    flow->scroll_y = 0u;
    flow->state_counter = 0u;
    flow->exit_wipe_progress = 0u;
    flow->state = 7u;
    return PORTABLE_GP_RESULTS_OK;
}

PortableGpResultsStatus portable_gp_results_flow_tick_record_slide(
    PortableGpResultsFlow *flow, uint8_t advance_triggered)
{
    static const uint8_t source_y[PORTABLE_GP_RESULTS_SLIDE_BAND_COUNT] = {
        0u, 2u, 4u, 6u, 8u, 10u, 12u, 15u, 17u};
    static const uint8_t start_x[PORTABLE_GP_RESULTS_SLIDE_BAND_COUNT] = {
        32u, 35u, 38u, 41u, 44u, 47u, 50u, 56u, 59u};
    if (flow == NULL)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    if (flow->state != 7u)
        return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    flow->record_slide_pending = 0u;
    flow->slide_band_count = 0u;
    flow->map_dirty_pending = 0u;
    flow->advance_skip_pending = 0u;
    ++flow->total_ticks;
    ++flow->state_counter;
    if (flow->state_counter > 0u)
    {
        flow->state_counter = 0u;
        ++flow->record_slide_progress;
        if (flow->record_slide_progress > 59u)
        {
            flow->state = 8u;
            flow->record_slide_progress = 0u;
        }
        else
        {
            for (uint8_t index = 0u;
                 index < PORTABLE_GP_RESULTS_SLIDE_BAND_COUNT; ++index)
            {
                const uint8_t x = start_x[index] > flow->record_slide_progress ?
                    (uint8_t)(start_x[index] - flow->record_slide_progress) : 0u;
                flow->slide_bands[index].source_y = source_y[index];
                flow->slide_bands[index].row_count = 2u;
                flow->slide_bands[index].destination_x = x;
            }
            flow->slide_band_count = PORTABLE_GP_RESULTS_SLIDE_BAND_COUNT;
            flow->record_slide_pending = 1u;
            flow->map_dirty_pending = 1u;
            if (flow->record_slide_progress <= 32u)
                flow->scroll_x = (uint16_t)(
                    (32u - flow->record_slide_progress) * 8u + 0x6cu);
            flow->scroll_y = 0u;
        }
    }
    if (advance_triggered != 0u)
    {
        flow->state = 9u;
        flow->advance_skip_pending = 1u;
    }
    return PORTABLE_GP_RESULTS_OK;
}

PortableGpResultsStatus portable_gp_results_flow_tick_menu_reveal(
    PortableGpResultsFlow *flow, uint8_t advance_triggered)
{
    if (flow == NULL)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    if (flow->state != 8u)
        return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    flow->menu_reveal_pending = 0u;
    flow->map_dirty_pending = 0u;
    flow->advance_skip_pending = 0u;
    ++flow->total_ticks;
    if (flow->menu_reveal_progress >= 5u)
    {
        flow->state = 9u;
        flow->menu_reveal_progress = 0u;
    }
    else
    {
        ++flow->menu_reveal_progress;
        flow->menu_reveal_pending = 1u;
        flow->map_dirty_pending = 1u;
    }
    if (advance_triggered != 0u)
    {
        flow->state = 9u;
        flow->advance_skip_pending = 1u;
    }
    return PORTABLE_GP_RESULTS_OK;
}

PortableGpResultsStatus portable_gp_results_flow_tick_menu_setup(
    PortableGpResultsFlow *flow, uint16_t race_flags)
{
    if (flow == NULL)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    if (flow->state != 9u)
        return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    ++flow->total_ticks;
    flow->menu_setup_graphics_pending = 1u;
    flow->menu_setup_map_pending = 1u;
    flow->menu_setup_scroll_map_pending = 1u;
    flow->menu_palette_pending = 0u;
    flow->menu_highlight_pending = 0u;
    flow->menu_highlight_x = 0u;
    flow->menu_highlight_y = 0u;
    flow->menu_highlight_width = 0u;
    flow->menu_highlight_height = 0u;
    flow->menu_highlight_palette_bank = 0u;
    if ((race_flags & UINT16_C(2)) != 0u)
    {
        flow->menu_palette_pending = 1u;
        flow->menu_highlight_pending = 1u;
        flow->menu_highlight_x = 22u;
        flow->menu_highlight_y = 11u;
        flow->menu_highlight_width = 7u;
        flow->menu_highlight_height = 2u;
        flow->menu_highlight_palette_bank = 4u;
    }
    flow->state = 10u;
    return PORTABLE_GP_RESULTS_OK;
}

PortableGpResultsStatus portable_gp_results_flow_tick_menu_record_setup(
    PortableGpResultsFlow *flow)
{
    if (flow == NULL)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    if (flow->state != 10u)
        return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    if (flow->record_row_count != PORTABLE_GP_RESULTS_RECORD_ROW_COUNT)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    ++flow->total_ticks;
    flow->track_name_pending = 1u;
    flow->followup_object_pending = 1u;
    flow->record_rows_pending = 1u;
    flow->map_dirty_pending = 1u;
    flow->scroll_x = UINT16_C(0x006c);
    flow->scroll_y = 0u;
    flow->state_counter = 0u;
    flow->exit_wipe_progress = 0u;
    flow->state = 11u;
    return PORTABLE_GP_RESULTS_OK;
}

PortableGpResultsStatus portable_gp_results_flow_tick_menu_selection(
    PortableGpResultsFlow *flow, uint16_t repeat_keys,
    uint16_t triggered_keys, uint16_t race_flags, const uint8_t option_codes[5])
{
    /* TT 22704 and Quick Run 2123C use the same DB6FC/DB7xx action order. */
    uint8_t previous;
    uint8_t disabled;
    if (flow == NULL)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    if (flow->state != 11u)
        return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    if(option_codes==NULL)return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    for(unsigned i=0u;i<5u;++i)if(option_codes[i]>4u)return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    if (flow->menu_selection >= PORTABLE_GP_RESULTS_MENU_OPTION_COUNT)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    ++flow->total_ticks;
    flow->menu_cursor_pending = 1u;
    flow->menu_selection_changed = 0u;
    flow->menu_selection_highlight_pending = 0u;
    flow->menu_disabled_pending = 0u;
    flow->menu_navigation_song_pending = 0u;
    flow->menu_confirm_song_pending = 0u;
    previous = flow->menu_selection;
    flow->menu_previous_selection = previous;
    disabled = (uint8_t)(option_codes[previous] == 3u &&
                         (race_flags & UINT16_C(2)) != 0u);
    flow->menu_disabled_pending = disabled;
    flow->menu_selection_highlight_pending = (uint8_t)!disabled;
    if ((repeat_keys & UINT16_C(0x0040)) != 0u)
        flow->menu_selection = flow->menu_selection == 0u ?
            PORTABLE_GP_RESULTS_MENU_OPTION_COUNT - 1u :
            (uint8_t)(flow->menu_selection - 1u);
    if ((repeat_keys & UINT16_C(0x0080)) != 0u)
        flow->menu_selection =
            (uint8_t)((flow->menu_selection + 1u) %
                      PORTABLE_GP_RESULTS_MENU_OPTION_COUNT);
    if (flow->menu_selection != previous)
    {
        flow->menu_cursor_pending = 0u;
        flow->menu_selection_highlight_pending = 0u;
        flow->menu_selection_changed = 1u;
        flow->menu_navigation_song_pending = 1u;
        flow->menu_navigation_song_id = UINT16_C(141);
    }
    flow->menu_action_code = option_codes[flow->menu_selection];
    if ((triggered_keys & UINT16_C(9)) != 0u)
    {
        flow->menu_confirm_song_pending = 1u;
        if (flow->menu_action_code == 3u &&
            (race_flags & UINT16_C(2)) != 0u)
            flow->menu_confirm_song_id = UINT16_C(145);
        else
        {
            flow->menu_confirm_song_id = UINT16_C(143);
            flow->state = 12u;
        }
    }
    flow->map_dirty_pending = 1u;
    return PORTABLE_GP_RESULTS_OK;
}

PortableGpResultsStatus portable_gp_results_flow_tick_action_dispatch(
    PortableGpResultsFlow *flow)
{
    static const uint8_t dispatch_codes[5] = {1u, 2u, 3u, 6u, 4u};
    if (flow == NULL)
        return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    if (flow->state != 12u)
        return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    ++flow->total_ticks;
    flow->action_dispatch_pending = 0u;
    flow->scene_request_pending = 0u;
    flow->scene_request_value = 0u;
    if (flow->menu_action_code < 5u)
    {
        flow->action_dispatch_pending = 1u;
        flow->action_dispatch_code = dispatch_codes[flow->menu_action_code];
        flow->scene_request_pending = 1u;
        flow->scene_request_value = UINT32_C(1);
    }
    flow->state = 13u;
    return PORTABLE_GP_RESULTS_OK;
}

void portable_gp_time_board_init(PortableGpTimeBoardFlow *f)
{if(f){memset(f,0,sizeof *f);f->state=3u;}}
int portable_gp_time_board_can_confirm(const PortableGpTimeBoardFlow *f)
{return f&&(f->state==5u||(f->state>=9u&&f->state<=11u));}
PortableGpResultsStatus portable_gp_time_board_tick(PortableGpTimeBoardFlow *f,uint8_t confirm)
{
    if(!f)return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    ++f->ticks;
    switch(f->state){
    case 3:f->visible=0u;f->state=4u;break;
    case 4:
        for(unsigned i=0u;i<8u;++i){f->row_x[i]=32u;f->row_delay[i]=(uint8_t)(3u*i);}
        f->visible=1u;f->counter=0u;f->state=5u;break;
    case 5:
        for(unsigned i=0u;i<8u;++i){if(f->row_delay[i])--f->row_delay[i];else if(f->row_x[i])--f->row_x[i];}
        if(++f->counter>60u||confirm){f->counter=0u;f->state=6u;}break;
    case 6:f->visible=0u;f->state=7u;break;
    case 7:memset(f->row_x,0,sizeof f->row_x);f->visible=1u;f->counter=0u;f->state=8u;break;
    case 8:if(++f->counter>60u){f->counter=0u;f->subcounter=f->progress=0u;f->state=9u;}break;
    case 9:
        if(++f->subcounter>2u){f->subcounter=0u;if(++f->progress>4u){f->progress=0u;f->state=10u;}}
        if(confirm){f->state=13u;f->skip_transfer=1u;}break;
    case 10:
        if(++f->subcounter>2u){f->subcounter=0u;if(++f->progress>3u){f->progress=0u;f->state=11u;}}
        if(confirm){f->state=13u;f->skip_transfer=1u;}break;
    case 11:
        if(++f->counter>30u){f->counter=0u;f->state=12u;}
        if(confirm){f->state=13u;f->skip_transfer=1u;}break;
    case 12:case 13:break;
    default:return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    }
    return PORTABLE_GP_RESULTS_OK;
}

void portable_quick_results_opening_init(PortableQuickResultsOpening *f)
{
    if(!f)return;
    memset(f,0,sizeof *f);f->state=5u;
    for(unsigned i=0u;i<8u;++i){f->row_x[i]=32u;f->row_delay[i]=(uint8_t)(3u*i);}
}
PortableGpResultsStatus portable_quick_results_opening_tick(PortableQuickResultsOpening *f,uint8_t confirm)
{
    if(!f)return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    switch(f->state){
    case 5:
        for(unsigned i=0u;i<8u;++i){if(f->row_delay[i])--f->row_delay[i];else if(f->row_x[i])--f->row_x[i];}
        if(++f->counter>60u||confirm){f->counter=0u;f->state=6u;}break;
    case 6:f->state=7u;break;
    case 7:memset(f->row_x,0,sizeof f->row_x);f->state=9u;break;
    case 9:
        if(f->window_progress<5u)++f->window_progress;
        else f->state=8u;
        if(confirm)f->state=8u;
        break;
    case 8:f->window_progress=6u;f->state=10u;break;
    case 10:break;
    default:return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    }
    return PORTABLE_GP_RESULTS_OK;
}
