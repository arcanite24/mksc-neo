#include "menu_names.h"
#include "model_asset.h"

#include <math.h>
#include <string.h>

#include "json_reader.h"

extern const uint8_t portable_embed_menu_names_json[];
extern const size_t portable_embed_menu_names_json_size;

enum { NAME_CAPACITY = 40, NAME_LIMIT = 96 };

typedef struct
{
    uint8_t kind; /* 1 driver, 2 course, 3 cup */
    uint32_t key;
    char name[NAME_CAPACITY];
    uint8_t speed, weight; /* Drivers only: 1..5, or 0 when unknown. */
} NameEntry;

static NameEntry names[NAME_LIMIT];
static unsigned name_count;
static int names_state; /* 0 unloaded, 1 loaded, -1 invalid */

static int parse_entries(PortableJsonReader *r, uint8_t kind, const char *key_name,
                         NameEntry *out, unsigned *count)
{
    if (!portable_json_take(r, '[')) return 0;
    if (portable_json_take(r, ']')) return 1;
    for (;;)
    {
        if (*count >= NAME_LIMIT || !portable_json_take(r, '{')) return 0;
        NameEntry entry = {kind, 0u, {0}, 0u, 0u};
        unsigned seen = 0u;
        for (;;)
        {
            char field[16];
            if (!portable_json_string(r, field, sizeof field) || !portable_json_take(r, ':')) return 0;
            if (strcmp(field, key_name) == 0 && !(seen & 1u))
            {
                if (!portable_json_number(r, &entry.key)) return 0;
                seen |= 1u;
            }
            else if (strcmp(field, "name") == 0 && !(seen & 2u))
            {
                if (!portable_json_string(r, entry.name, sizeof entry.name) || entry.name[0] == '\0')
                    return 0;
                seen |= 2u;
            }
            else if (kind == 1u && (strcmp(field, "speed") == 0 || strcmp(field, "weight") == 0))
            {
                uint32_t value;
                if (!portable_json_number(r, &value) || value < 1u || value > 5u) return 0;
                *(field[0] == 's' ? &entry.speed : &entry.weight) = (uint8_t)value;
            }
            else
                return 0;
            if (portable_json_take(r, '}')) break;
            if (!portable_json_take(r, ',')) return 0;
        }
        if (seen != 3u) return 0;
        out[(*count)++] = entry;
        if (portable_json_take(r, ']')) return 1;
        if (!portable_json_take(r, ',')) return 0;
    }
}

int portable_menu_names_parse(const char *json, size_t size)
{
    static NameEntry parsed[NAME_LIMIT];
    unsigned count = 0u;
    PortableJsonReader r = {json, json + size};
    if (json == NULL || !portable_json_take(&r, '{')) return 0;
    for (;;)
    {
        char key[16];
        if (!portable_json_string(&r, key, sizeof key) || !portable_json_take(&r, ':')) return 0;
        if (strcmp(key, "version") == 0)
        {
            uint32_t version;
            if (!portable_json_number(&r, &version) || version != 1u) return 0;
        }
        else if (strcmp(key, "note") == 0)
        {
            char note[512];
            if (!portable_json_string(&r, note, sizeof note)) return 0;
        }
        else if (strcmp(key, "drivers") == 0)
        {
            if (!parse_entries(&r, 1u, "id", parsed, &count)) return 0;
        }
        else if (strcmp(key, "courses") == 0)
        {
            if (!parse_entries(&r, 2u, "id", parsed, &count)) return 0;
        }
        else if (strcmp(key, "cups") == 0)
        {
            if (!parse_entries(&r, 3u, "cup", parsed, &count)) return 0;
        }
        else
            return 0;
        if (portable_json_take(&r, '}')) break;
        if (!portable_json_take(&r, ',')) return 0;
    }
    portable_json_whitespace(&r);
    if (r.at != r.end) return 0;
    memcpy(names, parsed, sizeof names);
    name_count = count;
    names_state = 1;
    return 1;
}

/* 3D-domain drivers named by their kind-110 rows, after the table. */
static PortableDriver3D drivers_3d[PORTABLE_DRIVER_3D_CAPACITY];
static size_t driver_3d_count;
static PortableKart3D karts_3d[PORTABLE_KART_3D_CAPACITY];
static size_t kart_3d_count;

void portable_menu_names_set_3d_drivers(const PortableAssetPackView *pack_3d)
{
    driver_3d_count = pack_3d != NULL ? portable_driver_3d_list(pack_3d, drivers_3d, PORTABLE_DRIVER_3D_CAPACITY) : 0u;
    kart_3d_count = pack_3d != NULL ? portable_kart_3d_list(pack_3d, karts_3d, PORTABLE_KART_3D_CAPACITY) : 0u;
}

static const NameEntry *find(uint8_t kind, uint32_t key)
{
    if (names_state == 0 &&
        !portable_menu_names_parse((const char *)portable_embed_menu_names_json,
                                   portable_embed_menu_names_json_size))
        names_state = -1;
    for (unsigned i = 0u; i < name_count; ++i)
        if (names[i].kind == kind && names[i].key == key) return &names[i];
    return NULL;
}

static const char *lookup(uint8_t kind, uint32_t key)
{
    const NameEntry *entry = find(kind, key);
    if (entry == NULL && kind == 1u)
        for (size_t i = 0u; i < driver_3d_count; ++i)
            if (drivers_3d[i].id == key) return drivers_3d[i].name;
    return entry != NULL ? entry->name : NULL;
}

static unsigned rating(double value)
{
    const long rounded = lround(value);
    return rounded < 1 ? 1u : rounded > 5 ? 5u : (unsigned)rounded;
}

int portable_menu_driver_stats(uint32_t driver_id, unsigned *speed, unsigned *weight)
{
    const NameEntry *entry = find(1u, driver_id);
    if (entry == NULL)
        for (size_t i = 0u; i < driver_3d_count; ++i)
            if (drivers_3d[i].id == driver_id)
            {
                /* Mario's ratios (1, 1) rate 3; the DS extremes reach 1 and 5. */
                *speed = rating(3.0 + (drivers_3d[i].speed_q16 / 65536.0 - 1.0) / 0.0225);
                *weight = rating(1.0 + (drivers_3d[i].mass_q16 / 65536.0 - 0.44) / 0.28);
                return 1;
            }
    if (entry == NULL || entry->speed == 0u || entry->weight == 0u) return 0;
    *speed = entry->speed;
    *weight = entry->weight;
    return 1;
}

/* Rates value 1..5 by where it sits between the listed karts' extremes. */
static unsigned kart_rating(size_t field, int32_t value)
{
    int32_t low = value, high = value;
    for (size_t i = 0u; i < kart_3d_count; ++i)
    {
        const int32_t *stats = &karts_3d[i].speed_q16;
        low = stats[field] < low ? stats[field] : low;
        high = stats[field] > high ? stats[field] : high;
    }
    return high == low ? 3u : rating(1.0 + 4.0 * (double)(value - low) / (double)(high - low));
}

const char *portable_menu_kart(uint32_t kart_id, unsigned ratings[3])
{
    for (size_t i = 0u; i < kart_3d_count; ++i)
        if (karts_3d[i].id == kart_id)
        {
            const int32_t *stats = &karts_3d[i].speed_q16;
            for (size_t field = 0u; field < 3u; ++field)
                ratings[field] = kart_rating(field, stats[field]);
            return karts_3d[i].name;
        }
    return NULL;
}

const char *portable_menu_driver_name(uint32_t driver_id)
{
    return lookup(1u, driver_id);
}

const char *portable_menu_course_name(uint32_t course_id)
{
    return lookup(2u, course_id);
}

const char *portable_menu_cup_name(unsigned cup)
{
    return lookup(3u, cup);
}
