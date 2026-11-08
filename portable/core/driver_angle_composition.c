#include "driver_angle_composition.h"

#include "driver_angle_target.h"

#include <stdint.h>

int portable_driver_angle_compose(
    const PortableDriverAngleCompositionInput *input,
    PortableDriverAngleCompositionResult *out)
{
    int status;

    if (input == NULL || out == NULL)
        return PORTABLE_DRIVER_ANGLE_COMPOSITION_INVALID_ARGUMENT;

    *out = (PortableDriverAngleCompositionResult){
        .current_angle_56 = input->current_angle_56,
        .target_angle_7a = input->target_angle_7a,
        .source_flags_b4 = input->source_flags_b4,
        .next_current_angle_56 = input->current_angle_56,
        .next_target_angle_7a = input->target_angle_7a,
        .next_flags_b4 = input->source_flags_b4,
        .next_limit_b6 = input->limit_b6,
        .next_position_82 = input->position_82,
        .next_angle_delta_80 = input->angle_delta_80,
        .next_timer_84 = input->timer_84,
        .next_response_status_7e = input->response_status_7e,
        .callback_status = PORTABLE_DRIVER_ANGLE_CALLBACK_OK,
    };

    if ((input->source_flags_b4 & UINT16_C(0x0a00)) != 0)
    {
        out->branch = PORTABLE_DRIVER_ANGLE_COMPOSITION_STATE_CALLBACK;
        status = portable_driver_angle_limit_callback(
            input->limit_b6, input->source_flags_b4, input->position_82,
            &out->limit_callback);
        out->callback_status = status;
        if (status != PORTABLE_DRIVER_ANGLE_CALLBACK_OK)
            return PORTABLE_DRIVER_ANGLE_COMPOSITION_LIMIT_CALLBACK_ERROR;

        out->next_flags_b4 = out->limit_callback.next_flags_b4;
        out->next_limit_b6 = out->limit_callback.next_limit_b6;
        out->next_position_82 = out->limit_callback.next_position_82;
        if (out->limit_callback.branch ==
            PORTABLE_DRIVER_ANGLE_LIMIT_BOUNDARY_RESET)
        {
            out->next_angle_delta_80 = 0;
            out->next_timer_84 = 0;
            out->next_response_status_7e = 0;
        }

        /* BC00 reads the pre-callback +0x7a value here. The addition is a
         * halfword store, even though the native calculation is widened. */
        out->next_current_angle_56 = (uint16_t)(
            out->next_position_82 + input->target_angle_7a);
        return PORTABLE_DRIVER_ANGLE_COMPOSITION_OK;
    }

    if (input->current_angle_56 == input->target_angle_7a)
    {
        out->branch = PORTABLE_DRIVER_ANGLE_COMPOSITION_EQUAL_CALLBACK;
        status = portable_driver_angle_target_callback(
            input->target_angle_ca, input->target_angle_7a,
            input->table_nibble_9a, input->target_table,
            input->target_table_count, &out->target_callback);
        out->callback_status = status;
        if (status != PORTABLE_DRIVER_ANGLE_CALLBACK_OK)
            return PORTABLE_DRIVER_ANGLE_COMPOSITION_TARGET_CALLBACK_ERROR;

        out->next_current_angle_56 = out->target_callback.next_angle_56;
        out->next_target_angle_7a = out->target_callback.next_angle_7a;
        return PORTABLE_DRIVER_ANGLE_COMPOSITION_OK;
    }

    /* P070 owns the signed halfword difference and non-callback branch
     * selection. At this point it cannot return either callback status. */
    {
        PortableDriverAngleTargetResult target_result;

        status = portable_driver_angle_target_update(
            input->current_angle_56, input->target_angle_7a,
            input->source_flags_b4, &target_result);
        if (status != PORTABLE_DRIVER_ANGLE_TARGET_OK)
            return PORTABLE_DRIVER_ANGLE_COMPOSITION_INVALID_ARGUMENT;

        out->next_current_angle_56 = target_result.next_current_angle_56;
        out->next_target_angle_7a = target_result.next_target_angle_7a;
        if (target_result.branch ==
            PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_SNAP_CURRENT)
            out->branch = PORTABLE_DRIVER_ANGLE_COMPOSITION_SNAP_CURRENT;
        else
            out->branch = PORTABLE_DRIVER_ANGLE_COMPOSITION_RETARGET_FAR;
    }

    return PORTABLE_DRIVER_ANGLE_COMPOSITION_OK;
}
