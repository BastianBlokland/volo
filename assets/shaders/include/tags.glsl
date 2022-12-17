#ifndef INCLUDE_TAGS
#define INCLUDE_TAGS

#include "types.glsl"

#define tag_background_bit 0
#define tag_terrain_bit 1
#define tag_geometry_bit 2
#define tag_vfx_bit 3
#define tag_ui_bit 4
#define tag_debug_bit 5
#define tag_unit_bit 6
#define tag_selected_bit 7
#define tag_damaged_bit 8

/**
 * Query if the given tag bit is set.
 */
bool tag_is_set(const u32 tags, const u32 tagBit) { return (tags & (1 << tagBit)) != 0; }

#endif // INCLUDE_TAGS
