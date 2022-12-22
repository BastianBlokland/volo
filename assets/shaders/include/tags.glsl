#ifndef INCLUDE_TAGS
#define INCLUDE_TAGS

#include "types.glsl"

#define tag_terrain_bit 0
#define tag_geometry_bit 1
#define tag_vfx_bit 2
#define tag_ui_bit 3
#define tag_debug_bit 4
#define tag_unit_bit 5
#define tag_selected_bit 6
#define tag_damaged_bit 7

/**
 * Query if the given tag bit is set.
 */
bool tag_is_set(const u32 tags, const u32 tagBit) { return (tags & (1 << tagBit)) != 0; }

/**
 * Encode tags for outputting into a R8UNORM texture
 * NOTE: Only the first 8 tags can be stored this way.
 */
f32 tags_tex_encode(const u32 tags) { return f32(tags & 0xFF) / 255.0; }
u32 tags_tex_decode(const f32 texNorm) { return u32(texNorm * 255.999); }

#endif // INCLUDE_TAGS
