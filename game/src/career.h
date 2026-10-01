/* career.h - what a player keeps between runs (CAREER.DAT, a dgk save):
 * money, deliveries, and the garage's items: paint jobs, horns, decals,
 * licences for the flatbed and the tanker, and a bigger cab. Each item has
 * a price and a rarity (its colour in the garage, from common grey to
 * legendary gold); bought, it is owned for good, and one of each kind is
 * fitted. */
#ifndef FW_CAREER_H
#define FW_CAREER_H

#include "dgk/base.h"

enum { KIND_PAINT, KIND_HORN, KIND_DECAL, KIND_LICENCE, KIND_CAB, KINDS };
enum { RARITY_COMMON, RARITY_UNCOMMON, RARITY_RARE, RARITY_EPIC, RARITY_LEGENDARY };

typedef struct item {
    int kind;
    const char *name;
    int price, rarity;
    uint32_t value;             /* paint: cab colour (0: rainbow); horn, decal, cab: which; licence: trailer type */
} item;

extern const item items[];
extern const int nitems;
extern const char *const kind_name[KINDS];
extern const char *const rarity_name[];
extern const uint32_t rarity_rgba[];

#define CAREER_MAGIC 0x52435746u    /* "FWCR" */
#define CAREER_VERSION 1

/* Fixed-size fields only: the same bytes from DJGPP and from Linux. */
typedef struct career {
    uint32_t money, delivered, perfect, metres;
    uint32_t owned;                 /* a bit per item */
    uint32_t fitted[KINDS];         /* the item fitted, of each kind */
    uint32_t reserved[3];
} career;

void career_new(career *c);         /* $250, the starting items fitted */
int  career_owns(const career *c, int item);
int  career_fitted(const career *c, int item);
/* Buy (and fit) an item: 0, or -1 short of money, -2 owned already. */
int  career_buy(career *c, int item);
int  career_fit(career *c, int item);    /* an owned one: 0, else -1 */
uint32_t career_licences(const career *c);   /* a bit per trailer type */
const item *career_item(const career *c, int kind);   /* the fitted one */

#endif
