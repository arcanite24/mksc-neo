#include "gp_ceremony_asset.h"
#include <string.h>
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
/* Bounds are checked for each containing metadata block before advancing. */
static uint16_t read_u16(const uint8_t *bytes, size_t *offset)
{
    const uint16_t value = (uint16_t)(bytes[*offset] | (uint16_t)bytes[*offset + 1u] << 8u);
    *offset += 2u;
    return value;
}

/* Image references are checked after the complete image bank has been read. */
static int ceremony_image_references_valid(const PortableGpCeremonyAssetView *view, uint32_t frames)
{
    for (unsigned cup = 0; cup < 5u; ++cup)
        for (unsigned i = 0; i < 23u; ++i)
            if (view->cup_headers[cup][i] >= view->image_count)
                return 0;
    for (unsigned i = 0; i < 8u; ++i)
        if (view->motion.flypast_images[i] >= view->image_count)
            return 0;
    for (unsigned rank = 0; rank < 3u; ++rank)
        for (unsigned i = 0; i < 16u; ++i)
            if (view->motion.trophy_images[rank][i] >= view->image_count)
                return 0;
    if (view->motion.drop_image >= view->image_count || view->shadow_image >= view->image_count ||
        view->summary_coin_image >= view->image_count)
        return 0;
    for (unsigned i = 0; i < 5u; ++i)
        if (view->cup_labels[i] >= view->image_count)
            return 0;
    for (unsigned i = 0; i < 12u; ++i)
        if (view->glyphs[i] >= view->image_count)
            return 0;
    for (unsigned i = 0; i < 8u; ++i)
        if (view->rank_labels[i] >= view->image_count)
            return 0;
    for (unsigned i = 0; i < 10u; ++i)
        if (view->coin_digits[i] >= view->image_count || view->total_digits[i] >= view->image_count)
            return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (view->highlight_classes[i] >= view->image_count)
            return 0;
    if (view->unlock_image >= view->image_count)
        return 0;
    for (unsigned i = 0; i < frames; ++i)
        if (view->motion.frames[i].image_id >= view->image_count)
            return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (view->panels[i] >= view->image_count)
            return 0;
    for (unsigned i = 0; i < 12u; ++i)
        if (view->sparkles[i] >= view->image_count || view->confetti[i] >= view->image_count)
            return 0;
    return 1;
}

/* GPC4 appends source motion metadata followed by a length-prefixed image bank.
 * The fixed metadata bound below covers every field read before variable images. */
static int read_source_presentation(const PortableAssetSectionView *section, size_t *read_offset,
                                    PortableGpCeremonyAssetView *next)
{
    const uint8_t *p = section->bytes;
    size_t offset = *read_offset;
    if (section->size - offset < 912u || u32(p + offset) != UINT32_C(0x31524543))
        return 0;
    const uint32_t stage = u32(p + offset + 4u), images = u32(p + offset + 8u),
                   frames = u32(p + offset + 12u);
    if (stage > 65535u || images > PORTABLE_CEREMONY_MAX_IMAGES - PORTABLE_CEREMONY_IMAGE_COUNT ||
        images == 0u || frames == 0u || frames > 32u ||
        section->size - offset < 912u + frames * 4u + images)
        return 0;
    next->source_presentation = 1u;
    next->stage_content_id = (uint16_t)stage;
    next->image_count = (uint16_t)(PORTABLE_CEREMONY_IMAGE_COUNT + images);
    offset += 16u;
    for (unsigned cup = 0; cup < 5u; ++cup)
        for (unsigned i = 0; i < 23u; ++i)
            next->cup_headers[cup][i] = read_u16(p, &offset);
    memcpy(next->motion.winner_header, next->cup_headers[0], sizeof next->motion.winner_header);
    memcpy(next->motion.loser_header, next->cup_headers[0] + 16u, sizeof next->motion.loser_header);
    for (unsigned i = 0; i < 8u; ++i)
        next->motion.flypast_images[i] = read_u16(p, &offset);
    for (unsigned rank = 0; rank < 3u; ++rank)
        for (unsigned i = 0; i < 16u; ++i)
            next->motion.trophy_images[rank][i] = read_u16(p, &offset);
    next->motion.zoom_image = read_u16(p, &offset);
    next->motion.drop_image = read_u16(p, &offset);
    next->motion.sparkle_image = read_u16(p, &offset);
    next->motion.confetti_image = read_u16(p, &offset);
    memcpy(next->motion.trophy_delay, p + offset, 3u);
    if (p[offset + 3u])
        return 0;
    offset += 4u;
    for (unsigned i = 0; i < 8u; ++i)
    {
        PortableCeremonyTrack *track = &next->motion.tracks[i];
        uint16_t value;
        value = read_u16(p, &offset);
        track->x = (int16_t)(value < 32768u ? (int32_t)value : (int32_t)value - 65536);
        value = read_u16(p, &offset);
        track->y = (int16_t)(value < 32768u ? (int32_t)value : (int32_t)value - 65536);
        value = read_u16(p, &offset);
        track->depth_bias = (int16_t)(value < 32768u ? (int32_t)value : (int32_t)value - 65536);
        track->first = read_u16(p, &offset);
        track->count = read_u16(p, &offset);
        track->initial_frame = read_u16(p, &offset);
        track->initial_remaining = read_u16(p, &offset);
        value = read_u16(p, &offset);
        if (value || !track->count || (uint32_t)track->first + track->count > frames ||
            track->initial_frame >= track->count)
            return 0;
    }
    next->motion.frame_count = (uint16_t)frames;
    for (unsigned i = 0; i < frames; ++i)
    {
        next->motion.frames[i].image_id = read_u16(p, &offset);
        next->motion.frames[i].duration = read_u16(p, &offset);
    }
    for (unsigned i = 0; i < 3u; ++i)
        next->panels[i] = read_u16(p, &offset);
    uint16_t reserved;
    reserved = read_u16(p, &offset);
    if (reserved)
        return 0;
    for (unsigned i = 0; i < 12u; ++i)
        next->sparkles[i] = read_u16(p, &offset);
    for (unsigned i = 0; i < 12u; ++i)
        next->confetti[i] = read_u16(p, &offset);
    for (unsigned rank = 0; rank < 3u; ++rank)
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            const uint32_t bits = u32(p + offset);
            offset += 4u;
            next->podium_offsets[rank][axis] =
                bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
        }
    next->shadow_image = read_u16(p, &offset);
    next->summary_coin_image = read_u16(p, &offset);
    for (unsigned i = 0; i < 23u; ++i)
        for (unsigned axis = 0; axis < 2u; ++axis)
        {
            uint16_t bits;
            bits = read_u16(p, &offset);
            next->header_targets[i][axis] =
                (int16_t)(bits < 32768u ? (int32_t)bits : (int32_t)bits - 65536);
            if (next->header_targets[i][axis] < -512 || next->header_targets[i][axis] > 512)
                return 0;
        }
    for (unsigned i = 0; i < 5u; ++i)
        next->cup_labels[i] = read_u16(p, &offset);
    for (unsigned i = 0; i < 12u; ++i)
        next->glyphs[i] = read_u16(p, &offset);
    for (unsigned i = 0; i < 8u; ++i)
        next->rank_labels[i] = read_u16(p, &offset);
    for (unsigned i = 0; i < 10u; ++i)
        next->coin_digits[i] = read_u16(p, &offset);
    for (unsigned i = 0; i < 10u; ++i)
        next->total_digits[i] = read_u16(p, &offset);
    for (unsigned i = 0; i < 3u; ++i)
        next->highlight_classes[i] = read_u16(p, &offset);
    next->unlock_image = read_u16(p, &offset);
    for (unsigned group = 0; group < 2u; ++group)
        for (unsigned endpoint = 0; endpoint < 2u; ++endpoint)
            for (unsigned i = 0; i < 16u; ++i)
                next->pulse_colors[group][endpoint][i] = read_u16(p, &offset);
    memcpy(next->motion.header_targets, next->header_targets, sizeof next->header_targets);
    for (unsigned i = 0; i < images; ++i)
    {
        if (p[offset] > 1u)
            return 0;
        next->image_darken[PORTABLE_CEREMONY_IMAGE_COUNT + i] = p[offset++];
    }
    for (unsigned index = PORTABLE_CEREMONY_IMAGE_COUNT; index < next->image_count; ++index)
    {
        if (section->size - offset < 4u)
            return 0;
        const uint32_t size = u32(p + offset);
        offset += 4u;
        if (size > section->size - offset ||
            !portable_native_image_open(p + offset, size, &next->images[index]) ||
            next->images[index].width > 512u || next->images[index].height > 512u ||
            next->images[index].anchor_x < -512 || next->images[index].anchor_x > 512 ||
            next->images[index].anchor_y < -512 || next->images[index].anchor_y > 512)
            return 0;
        offset += size;
    }
    if (!ceremony_image_references_valid(next, frames))
        return 0;
    *read_offset = offset;
    return 1;
}

PortableGpCeremonyAssetStatus
portable_gp_ceremony_asset_open(const PortableAssetSectionView *section,
                                PortableGpCeremonyAssetView *out)
{
    PortableGpCeremonyAssetView next = {0};
    if (section == NULL || out == NULL)
        return PORTABLE_GP_CEREMONY_ASSET_INVALID_ARGUMENT;
    if (section->kind != PORTABLE_ASSET_SECTION_GP_CEREMONY_GRAPHICS || section->content_id != 1u ||
        (section->version != 3u && section->version != 4u) || section->bytes == NULL ||
        section->size < 32u)
        return PORTABLE_GP_CEREMONY_ASSET_INVALID_SECTION;
    const uint8_t *p = section->bytes;
    if (u32(p) != (section->version == 4u ? UINT32_C(0x34435047) : UINT32_C(0x33435047)) ||
        u32(p + 4) != PORTABLE_CEREMONY_IMAGE_COUNT)
        return PORTABLE_GP_CEREMONY_ASSET_INVALID_SECTION;
    next.entrance_ticks = u32(p + 8);
    next.celebration_ticks = u32(p + 12);
    next.fade_ticks = u32(p + 16);
    next.sky_color = u32(p + 20);
    next.ground_color = u32(p + 24);
    next.podium_color = u32(p + 28);
    if (next.entrance_ticks == 0u || next.entrance_ticks > 3600u || next.celebration_ticks == 0u ||
        next.celebration_ticks > 3600u || next.fade_ticks == 0u || next.fade_ticks > 600u ||
        (next.sky_color >> 24) != 255u || (next.ground_color >> 24) != 255u ||
        (next.podium_color >> 24) != 255u)
        return PORTABLE_GP_CEREMONY_ASSET_INVALID_SECTION;
    size_t offset = 32u;
    for (unsigned index = 0; index < PORTABLE_CEREMONY_IMAGE_COUNT; ++index)
    {
        if (section->size - offset < 4u)
            return PORTABLE_GP_CEREMONY_ASSET_INVALID_SECTION;
        const uint32_t size = u32(p + offset);
        offset += 4u;
        if (size > section->size - offset ||
            !portable_native_image_open(p + offset, size, &next.images[index]) ||
            next.images[index].width > 240u || next.images[index].height > 160u ||
            next.images[index].anchor_x < -240 || next.images[index].anchor_x > 240 ||
            next.images[index].anchor_y < -160 || next.images[index].anchor_y > 160)
            return PORTABLE_GP_CEREMONY_ASSET_INVALID_SECTION;
        offset += size;
    }
    next.image_count = PORTABLE_CEREMONY_IMAGE_COUNT;
    if (section->version == 4u)
    {
        if (!read_source_presentation(section, &offset, &next))
            return PORTABLE_GP_CEREMONY_ASSET_INVALID_SECTION;
    }
    if (offset != section->size)
        return PORTABLE_GP_CEREMONY_ASSET_INVALID_SECTION;
    *out = next;
    return PORTABLE_GP_CEREMONY_ASSET_OK;
}
