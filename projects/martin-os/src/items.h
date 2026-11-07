// The groups and items the wear counter shows. Placeholder names for now:
// edit them to match the real clothes. Up to 8 items per group.
//
// Counts are saved by group and item name: reordering or adding items keeps
// their counts, renaming an item starts it from 0.
#pragma once
#include "radial.h"

struct Group {
  const char *name;                // up to about 10 characters fits
  const char *items[RADIAL_MAX];   // fewer than 8 is fine
};

const Group GROUPS[] = {
    {"Merino", {"Black", "Navy", "Grey", "Olive", "Charcoal", "White"}},
    {"Formal", {"White", "Light blue", "Striped"}},
    {"Pants", {"Jeans", "Chinos", "Black"}},
    {"Bed", {"Pillowcase", "Sheets"}},
};
constexpr int NUM_GROUPS = sizeof GROUPS / sizeof GROUPS[0];
