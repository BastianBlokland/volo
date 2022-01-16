#include "asset_font.h"
#include "core_alloc.h"
#include "core_compare.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_search.h"
#include "core_utf8.h"
#include "ecs_utils.h"
#include "ecs_world.h"

#include "repo_internal.h"

ecs_comp_define_public(AssetFontComp);

static void ecs_destruct_font_comp(void* data) {
  AssetFontComp* comp = data;
  alloc_free_array_t(g_alloc_heap, comp->characters, comp->characterCount);
  alloc_free_array_t(g_alloc_heap, comp->points, comp->pointCount);
  alloc_free_array_t(g_alloc_heap, comp->segments, comp->segmentCount);
  alloc_free_array_t(g_alloc_heap, comp->glyphs, comp->glyphCount);
}

ecs_view_define(UnloadView) {
  ecs_access_with(AssetFontComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any font-asset components for unloaded assets.
 */
ecs_system_define(UnloadFontAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetFontComp);
  }
}

ecs_module_init(asset_font_module) {
  ecs_register_comp(AssetFontComp, .destructor = ecs_destruct_font_comp);

  ecs_register_view(UnloadView);

  ecs_register_system(UnloadFontAssetSys, ecs_view_id(UnloadView));
}

i8 asset_font_compare_char(const void* a, const void* b) {
  return compare_u32(field_ptr(a, AssetFontChar, cp), field_ptr(b, AssetFontChar, cp));
}

const AssetFontGlyph* asset_font_lookup(const AssetFontComp* font, const UnicodeCp cp) {
  const AssetFontChar* ch = search_binary_t(
      font->characters,
      font->characters + font->characterCount,
      AssetFontChar,
      asset_font_compare_char,
      mem_struct(AssetFontChar, .cp = cp).ptr);

  if (UNLIKELY(!ch)) {
    return &font->glyphs[0]; // Return the 'missing' glyph (guaranteed to exist).
  }
  diag_assert(ch->glyphIndex < font->glyphCount);
  return &font->glyphs[ch->glyphIndex];
}

usize asset_font_lookup_utf8(
    const AssetFontComp* font, String text, const AssetFontGlyph** out, const usize outCount) {

  if (UNLIKELY(!text.size)) {
    return 0;
  }
  usize count = 0;
  do {
    UnicodeCp cp;
    text = utf8_cp_read(text, &cp);
    if (out) {
      if (UNLIKELY(count >= outCount)) {
        return count;
      }
      out[count] = asset_font_lookup(font, cp);
    }
    ++count;
  } while (text.size);

  return count;
}

static f32 font_math_dist_sqr(const AssetFontPoint s, const AssetFontPoint e) {
  const f32 dX = s.x - e.x;
  const f32 dY = s.y - e.y;
  return dX * dX + dY * dY;
}

static f32 font_math_dist(const AssetFontPoint s, const AssetFontPoint e) {
  const f32 distSqr = font_math_dist_sqr(s, e);
  return math_sqrt_f32(distSqr);
}

static AssetFontPoint font_math_lerp(const AssetFontPoint s, const AssetFontPoint e, const f32 t) {
  const f32 x = math_lerp(s.x, e.x, t);
  const f32 y = math_lerp(s.y, e.y, t);
  return (AssetFontPoint){x, y};
}

static AssetFontPoint font_math_quad_bezier(
    const AssetFontPoint s, const AssetFontPoint c, const AssetFontPoint e, const f32 t) {
  const f32 invT = 1.0f - t;
  const f32 x    = c.x + (s.x - c.x) * invT * invT + (e.x - c.x) * t * t;
  const f32 y    = c.y + (s.y - c.y) * invT * invT + (e.y - c.y) * t * t;
  return (AssetFontPoint){x, y};
}

AssetFontPoint asset_font_seg_sample(const AssetFontComp* font, const usize index, const f32 t) {
  const AssetFontSegment* seg = &font->segments[index];
  switch (seg->type) {
  case AssetFontSegment_Line: {
    const AssetFontPoint s = font->points[seg->pointIndex + 0];
    const AssetFontPoint e = font->points[seg->pointIndex + 1];
    return font_math_lerp(s, e, t);
  }
  case AssetFontSegment_QuadraticBezier: {
    const AssetFontPoint s = font->points[seg->pointIndex + 0];
    const AssetFontPoint c = font->points[seg->pointIndex + 1];
    const AssetFontPoint e = font->points[seg->pointIndex + 2];
    return font_math_quad_bezier(s, c, e, t);
  }
  }
  diag_crash();
}

f32 asset_font_seg_length(const AssetFontComp* font, const usize index) {
  const AssetFontSegment* seg = &font->segments[index];
  switch (seg->type) {
  case AssetFontSegment_Line: {
    const AssetFontPoint s = font->points[seg->pointIndex + 0];
    const AssetFontPoint e = font->points[seg->pointIndex + 1];
    return font_math_dist(s, e);
  }
  case AssetFontSegment_QuadraticBezier: {
    const AssetFontPoint s = font->points[seg->pointIndex + 0];
    const AssetFontPoint c = font->points[seg->pointIndex + 1];
    const AssetFontPoint e = font->points[seg->pointIndex + 2];

    /**
     * Closed form analytical solutions for the arc-length of a quadratic bezier exist but are
     * pretty expensive. Instead we approximate it with a series of linear distances.
     *
     * More information: https://pomax.github.io/bezierinfo/#arclength
     */

    const u32      steps = 3;
    f32            dist  = 0;
    AssetFontPoint prev  = s;
    for (usize i = 1; i != steps; ++i) {
      const f32            t     = i / (f32)steps;
      const AssetFontPoint point = font_math_quad_bezier(s, c, e, t);
      dist += font_math_dist(prev, point);
      prev = point;
    }
    dist += font_math_dist(prev, e);
    return dist;
  }
  }
  diag_crash();
}
