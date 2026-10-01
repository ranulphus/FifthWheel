/* career.c - the player's career and the garage's items (see career.h). */
#include "career.h"
#include "vehicle.h"
#include <string.h>

const char *const kind_name[KINDS] = { "PAINT", "HORNS", "DECALS", "LICENCES", "CABS" };
const char *const rarity_name[] = { "COMMON", "UNCOMMON", "RARE", "EPIC", "LEGENDARY" };
const uint32_t rarity_rgba[] = { 0xC8C8D0FFu, 0x6FE07AFFu, 0x4FA8FFFFu, 0xC070FFFFu, 0xFFB030FFu };

/* The first of each kind is the starting one, owned and free. */
const item items[] = {
    { KIND_PAINT, "TOMATO RED", 0, RARITY_COMMON, 0xE8402Au },
    { KIND_PAINT, "SKY BLUE", 150, RARITY_COMMON, 0x3A9AE8u },
    { KIND_PAINT, "LIME", 200, RARITY_UNCOMMON, 0x7ED83Au },
    { KIND_PAINT, "SUNSHINE", 300, RARITY_UNCOMMON, 0xFFC21Eu },
    { KIND_PAINT, "GRAPE", 600, RARITY_RARE, 0x8A4AD8u },
    { KIND_PAINT, "MIDNIGHT", 900, RARITY_RARE, 0x26304Au },
    { KIND_PAINT, "GOLD RUSH", 2500, RARITY_EPIC, 0xE8B83Au },
    { KIND_PAINT, "RAINBOW", 6000, RARITY_LEGENDARY, 0 },
    { KIND_HORN, "TWO-TONE", 0, RARITY_COMMON, 0 },
    { KIND_HORN, "BIG AIR", 400, RARITY_UNCOMMON, 1 },
    { KIND_HORN, "JINGLE", 1200, RARITY_RARE, 2 },
    { KIND_HORN, "QUACK", 3000, RARITY_EPIC, 3 },
    { KIND_DECAL, "NONE", 0, RARITY_COMMON, 0 },
    { KIND_DECAL, "STRIPES", 250, RARITY_COMMON, 1 },
    { KIND_DECAL, "FLAMES", 1500, RARITY_RARE, 2 },
    { KIND_DECAL, "LIGHTNING", 2000, RARITY_EPIC, 3 },
    { KIND_DECAL, "STARS", 5000, RARITY_LEGENDARY, 4 },
    { KIND_LICENCE, "BOX TRAILERS", 0, RARITY_COMMON, TRAILER_BOX },
    { KIND_LICENCE, "FLATBEDS", 500, RARITY_UNCOMMON, TRAILER_FLATBED },
    { KIND_LICENCE, "TANKERS", 1500, RARITY_RARE, TRAILER_TANKER },
    { KIND_CAB, "STANDARD", 0, RARITY_COMMON, 0 },
    { KIND_CAB, "BIG CAB", 4000, RARITY_EPIC, 1 },
};
const int nitems = (int)DGK_ARRAY_LEN(items);

static int first_of(int kind)
{
    int i;
    for (i = 0; i < nitems && items[i].kind != kind; i++)
        continue;
    return i;
}

void career_new(career *c)
{
    int k;
    memset(c, 0, sizeof *c);
    c->money = 250;
    for (k = 0; k < KINDS; k++) {
        c->fitted[k] = (uint32_t)first_of(k);
        c->owned |= 1u << c->fitted[k];
    }
}

int career_owns(const career *c, int i)
{
    return i >= 0 && i < nitems && (c->owned >> i & 1u);
}

int career_fitted(const career *c, int i)
{
    return i >= 0 && i < nitems && c->fitted[items[i].kind] == (uint32_t)i;
}

int career_fit(career *c, int i)
{
    if (!career_owns(c, i))
        return -1;
    if (items[i].kind != KIND_LICENCE)          /* licences are all held at once */
        c->fitted[items[i].kind] = (uint32_t)i;
    return 0;
}

int career_buy(career *c, int i)
{
    if (i < 0 || i >= nitems)
        return -1;
    if (career_owns(c, i))
        return -2;
    if (c->money < (uint32_t)items[i].price)
        return -1;
    c->money -= (uint32_t)items[i].price;
    c->owned |= 1u << i;
    career_fit(c, i);
    return 0;
}

uint32_t career_licences(const career *c)
{
    uint32_t bits = 0;
    int i;
    for (i = 0; i < nitems; i++)
        if (items[i].kind == KIND_LICENCE && career_owns(c, i))
            bits |= 1u << items[i].value;
    return bits;
}

const item *career_item(const career *c, int kind)
{
    uint32_t i = c->fitted[kind];
    return &items[i < (uint32_t)nitems ? i : (uint32_t)first_of(kind)];
}
