#ifndef GEN2_ITEMS_H
#define GEN2_ITEMS_H

#include <stdint.h>
#include <stdbool.h>

// Gen 2 (Gold/Silver/Crystal) item helpers.

// Display name for a Gen 2 item id ("" for 0, "?" for unused ids).
const char *gen2_item_name(uint8_t item);

// The Gen 3 item id a Gen 2 held item becomes on transfer, or 0 when the item
// has no Gen 3 equivalent a Pokemon can hold (Gen 2 berries, mail, Apricorns,
// key items, Pink/Polkadot Bow, ...). Matched by name from pret's sources.
uint16_t gen2_item_to_gen3(uint8_t item);

// Gen 2 berries (Berry, Gold Berry, MiracleBerry, ...): none survive to Gen 3.
bool gen2_item_is_berry(uint8_t item);

#endif
