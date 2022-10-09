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

#if defined(VOLO_MSVC)
float sqrtf(float);

#pragma intrinsic(sqrtf)
#define intrinsic_sqrt_f32 sqrtf
#else
#define intrinsic_sqrt_f32 __builtin_sqrtf
#endif

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

const AssetFontGlyph* asset_font_missing(const AssetFontComp* font) {
  return &font->glyphs[0]; // The 'missing' glyph, is guaranteed to exist.
}

const AssetFontGlyph* asset_font_lookup(const AssetFontComp* font, const Unicode cp) {
  const AssetFontChar* ch = search_binary_t(
      font->characters,
      font->characters + font->characterCount,
      AssetFontChar,
      asset_font_compare_char,
      mem_struct(AssetFontChar, .cp = cp).ptr);

  if (UNLIKELY(!ch)) {
    return asset_font_missing(font);
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
    Unicode cp;
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

static f32 font_math_dot(const AssetFontPoint a, const AssetFontPoint b) {
  return (a.x * b.x) + (a.y * b.y);
}

static f32 font_math_dist_sqr(const AssetFontPoint start, const AssetFontPoint end) {
  const AssetFontPoint toEnd = {end.x - start.x, end.y - start.y};
  return font_math_dot(toEnd, toEnd);
}

static f32 font_math_dist(const AssetFontPoint start, const AssetFontPoint end) {
  const f32 distSqr = font_math_dist_sqr(start, end);
  return intrinsic_sqrt_f32(distSqr);
}

static AssetFontPoint
font_math_line_sample(const AssetFontPoint start, const AssetFontPoint end, const f32 t) {
  const f32 x = math_lerp(start.x, end.x, t);
  const f32 y = math_lerp(start.y, end.y, t);
  return (AssetFontPoint){x, y};
}

static AssetFontPoint font_math_quad_bezier_sample(
    const AssetFontPoint start, const AssetFontPoint ctrl, const AssetFontPoint end, const f32 t) {
  const f32 invT = 1.0f - t;
  const f32 x    = ctrl.x + (start.x - ctrl.x) * invT * invT + (end.x - ctrl.x) * t * t;
  const f32 y    = ctrl.y + (start.y - ctrl.y) * invT * invT + (end.y - ctrl.y) * t * t;
  return (AssetFontPoint){x, y};
}

INLINE_HINT static f32 font_math_line_dist_sqr(
    const AssetFontPoint start, const AssetFontPoint end, const AssetFontPoint point) {

  const f32 vX      = end.x - start.x;
  const f32 vY      = end.y - start.y;
  const f32 vMagSqr = vX * vX + vY * vY;

  f32 t = ((point.x - start.x) * vX + (point.y - start.y) * vY) / vMagSqr;
  if (t < 0) {
    t = 0;
  } else if (t > 1) {
    t = 1;
  }

  const f32 lx = start.x + t * vX;
  const f32 ly = start.y + t * vY;
  const f32 dx = point.x - lx;
  const f32 dy = point.y - ly;
  return dx * dx + dy * dy;
}

static bool font_math_line_inside(
    const AssetFontPoint start, const AssetFontPoint end, const AssetFontPoint point) {

  /**
   * Check whether the line intersects a line from (-inf, point.y) to (point.x, point.y).
   * Impl based on: https://stackoverflow.com/questions/11716268/point-in-polygon-algorithm
   * More info: http://erich.realtimerendering.com/ptinpoly/
   */

  // Check if the line crosses the horizontal line at y in either direction.
  if (((start.y <= point.y) && (end.y > point.y)) || ((end.y <= point.y) && (start.y > point.y))) {
    // Get the point where it crosses, and check if it crosses to the right of the given point.
    return ((end.x - start.x) * (point.y - start.y) / (end.y - start.y) + start.x) > point.x;
  }
  return false;
}

AssetFontPoint asset_font_seg_sample(const AssetFontComp* font, const usize index, const f32 t) {
  const AssetFontSegment* seg = &font->segments[index];
  switch (seg->type) {
  case AssetFontSegment_Line: {
    const AssetFontPoint start = font->points[seg->pointIndex + 0];
    const AssetFontPoint end   = font->points[seg->pointIndex + 1];
    return font_math_line_sample(start, end, t);
  }
  case AssetFontSegment_QuadraticBezier: {
    const AssetFontPoint start = font->points[seg->pointIndex + 0];
    const AssetFontPoint ctrl  = font->points[seg->pointIndex + 1];
    const AssetFontPoint end   = font->points[seg->pointIndex + 2];
    return font_math_quad_bezier_sample(start, ctrl, end, t);
  }
  }
  diag_crash();
}

f32 asset_font_seg_length(const AssetFontComp* font, const usize index) {
  const AssetFontSegment* seg = &font->segments[index];
  switch (seg->type) {
  case AssetFontSegment_Line: {
    const AssetFontPoint start = font->points[seg->pointIndex + 0];
    const AssetFontPoint end   = font->points[seg->pointIndex + 1];
    return font_math_dist(start, end);
  }
  case AssetFontSegment_QuadraticBezier: {
    const AssetFontPoint start = font->points[seg->pointIndex + 0];
    const AssetFontPoint ctrl  = font->points[seg->pointIndex + 1];
    const AssetFontPoint end   = font->points[seg->pointIndex + 2];

    /**
     * Closed form analytical solutions for the arc-length of a quadratic bezier exist but are
     * pretty expensive. Instead we approximate it with a series of linear distances.
     *
     * More information: https://pomax.github.io/bezierinfo/#arclength
     */

    const u32      steps = 3;
    f32            dist  = 0;
    AssetFontPoint prev  = start;
    for (usize i = 1; i != steps; ++i) {
      const f32            t     = i / (f32)steps;
      const AssetFontPoint point = font_math_quad_bezier_sample(start, ctrl, end, t);
      dist += font_math_dist(prev, point);
      prev = point;
    }
    dist += font_math_dist(prev, end);
    return dist;
  }
  }
  diag_crash();
}

f32 asset_font_glyph_dist(
    const AssetFontComp* font, const AssetFontGlyph* glyph, const AssetFontPoint point) {

  /**
   * Find the signed distance of the given point to the glyph.
   * Iterates over all segments and checks if the segment is closer then the current closest dist.
   *
   * Additionally we keep track if we're inside or outside the shape by counting how many segments
   * we're to the right of.
   */

  f32  minDistSqr = f32_max;
  bool inside     = false;
  for (usize seg = glyph->segmentIndex; seg != glyph->segmentIndex + glyph->segmentCount; ++seg) {
    switch (font->segments[seg].type) {
    case AssetFontSegment_Line: {
      const AssetFontPoint start   = font->points[font->segments[seg].pointIndex + 0];
      const AssetFontPoint end     = font->points[font->segments[seg].pointIndex + 1];
      const f32            distSqr = font_math_line_dist_sqr(start, end, point);
      minDistSqr                   = math_min(minDistSqr, distSqr);
      inside ^= font_math_line_inside(start, end, point);
      break;
    }
    case AssetFontSegment_QuadraticBezier: {
      const AssetFontPoint start = font->points[font->segments[seg].pointIndex + 0];
      const AssetFontPoint ctrl  = font->points[font->segments[seg].pointIndex + 1];
      const AssetFontPoint end   = font->points[font->segments[seg].pointIndex + 2];

      /**
       * Naive implementation that splits the quadratic bezier into a series of line segments.
       * Analytical solutions for quadratic beziers exist but have not been explored yet.
       */

      const u32      steps = 5;
      AssetFontPoint prev  = start;
      for (usize j = 1; j != steps; ++j) {
        const f32            t           = j / (f32)steps;
        const AssetFontPoint bezierPoint = font_math_quad_bezier_sample(start, ctrl, end, t);
        const f32            distSqr     = font_math_line_dist_sqr(prev, bezierPoint, point);
        minDistSqr                       = math_min(minDistSqr, distSqr);
        inside ^= font_math_line_inside(prev, bezierPoint, point);
        prev = bezierPoint;
      }
      const f32 distSqr = font_math_line_dist_sqr(prev, end, point);
      minDistSqr        = math_min(minDistSqr, distSqr);
      inside ^= font_math_line_inside(prev, end, point);
    }
    }
  }
  const f32 minDist = intrinsic_sqrt_f32(minDistSqr);
  return minDist * (inside ? -1.0f : 1.0f);
}
