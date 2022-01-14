#pragma once
#include "ecs_module.h"

typedef struct {
  u32 unicode;
  u32 glyphIndex;
} AssetFontCodepoint;

ecs_comp_extern_public(AssetFontComp) {
  struct {
    AssetFontCodepoint* values; // Sorted on the unicode value.
    usize               count;
  } codepoints;
};

/**
 * Compare two AssetFontCodepoint's.
 * Signature is compatible with the 'CompareFunc' from 'core_compare.h'.
 */
i8 asset_font_compare_codepoint(const void* a, const void* b);
