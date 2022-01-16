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

static f32 font_math_dot(const AssetFontPoint a, const AssetFontPoint b) {
  return (a.x * b.x) + (a.y * b.y);
}

static f32 font_math_dist_sqr(const AssetFontPoint start, const AssetFontPoint end) {
  const AssetFontPoint toEnd = {end.x - start.x, end.y - start.y};
  return font_math_dot(toEnd, toEnd);
}

static f32 font_math_dist(const AssetFontPoint start, const AssetFontPoint end) {
  const f32 distSqr = font_math_dist_sqr(start, end);
  return math_sqrt_f32(distSqr);
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

static f32 font_math_cuberoot(const f32 x) {
  if (x < 0.0f) {
    return -math_pow_f32(-x, 1.0f / 3.0f);
  }
  return math_pow_f32(x, 1.0f / 3.0f);
}

static u8 font_math_solve_cube(const f32 a, const f32 b, const f32 c, f32 out[3]) {
  const f32 p      = b - a * a / 3;
  const f32 q      = a * (2 * a * a - 9 * b) / 27 + c;
  const f32 p3     = p * p * p;
  const f32 d      = q * q + 4 * p3 / 27;
  const f32 offset = -a / 3;

  if (d >= 0.0f) {
    /**
     * Single real solution.
     */
    const f32 z = math_sqrt_f32(d);
    f32       u = (-q + z) / 2;
    f32       v = (-q - z) / 2;
    u           = font_math_cuberoot(u);
    v           = font_math_cuberoot(v);
    out[0]      = offset + u + v;
    return 1;
  }

  /**
   * Three real solution.
   */
  const f32 u = math_sqrt_f32(-p / 3);
  const f32 v = math_acos_f32(-math_sqrt_f32(-27 / p3) * q / 2) / 3;
  const f32 m = math_cos_f32(v), n = math_sin_f32(v) * 1.732050808f;
  out[0] = offset + u * (m + m);
  out[1] = offset - u * (n + m);
  out[2] = offset + u * (n - m);
  return 3;
}

static f32 font_math_line_dist(
    const AssetFontPoint start, const AssetFontPoint end, const AssetFontPoint point) {

  const AssetFontPoint ab   = {end.x - start.x, end.y - start.y};
  const AssetFontPoint be   = {point.x - end.x, point.y - end.y};
  const AssetFontPoint ae   = {point.x - start.x, point.y - start.y};
  const f32            abBe = (ab.x * be.x + ab.y * be.y);
  const f32            abAe = (ab.x * ae.x + ab.y * ae.y);

  if (abBe > 0) {
    const f32 y = point.y - end.y;
    const f32 x = point.x - end.x;
    return math_sqrt_f32(x * x + y * y);
  }
  if (abAe < 0) {
    const f32 y = point.y - start.y;
    const f32 x = point.x - start.x;
    return math_sqrt_f32(x * x + y * y);
  }

  // Finding the perpendicular distance
  const f32 mod = math_sqrt_f32(ab.x * ab.x + ab.y * ab.y);
  return math_abs(ab.x * ae.y - ab.y * ae.x) / mod;
}

static f32 font_math_quad_bezier_dist(
    const AssetFontPoint start,
    const AssetFontPoint ctrl,
    const AssetFontPoint end,
    const AssetFontPoint point) {

  const AssetFontPoint a = {start.x - 2.0f * ctrl.x + end.x, start.y - 2.0f * ctrl.y + end.y};
  const AssetFontPoint b = {2.0f * (start.x + ctrl.x), 2.0f * (start.y + ctrl.y)};
  const AssetFontPoint c = {start.x, start.y};
  const AssetFontPoint d = {c.x - point.x, c.y - point.y};

  const f32 k3 = 2.0f * font_math_dot(a, a);
  const f32 k2 = 3.0f * font_math_dot(a, b);
  const f32 k1 = font_math_dot(a, d) + font_math_dot(b, b);
  const f32 k0 = font_math_dot(b, d);

  f32      resSqr = f32_max;
  f32      roots[3];
  const u8 numRoots = font_math_solve_cube(k2 / k3, k1 / k3, k0 / k3, roots);
  for (u8 i = 0; i < numRoots; i++) {
    const f32            t = math_clamp_f32(roots[i], 0.0f, 1.0f);
    const AssetFontPoint p = {
        (1.0f - t) * (1.0f - t) * start.x + 2.0f * t * (1.0f - t) * ctrl.x + t * t * end.y,
        (1.0f - t) * (1.0f - t) * start.y + 2.0f * t * (1.0f - t) * ctrl.x + t * t * end.y,
    };
    const f32 distSqr = font_math_dist_sqr(p, point);
    if (distSqr < resSqr) {
      resSqr = distSqr;
    }
  }
  return math_sqrt_f32(resSqr);
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

f32 asset_font_seg_dist(const AssetFontComp* font, const usize index, const AssetFontPoint point) {
  const AssetFontSegment* seg = &font->segments[index];
  switch (seg->type) {
  case AssetFontSegment_Line: {
    (void)font_math_line_dist;
    // const AssetFontPoint start = font->points[seg->pointIndex + 0];
    // const AssetFontPoint end   = font->points[seg->pointIndex + 1];
    // return font_math_line_dist(start, end, point);
    return f32_max;
  }
  case AssetFontSegment_QuadraticBezier: {
    (void)font_math_quad_bezier_dist;
    const AssetFontPoint start = font->points[seg->pointIndex + 0];
    const AssetFontPoint ctrl  = font->points[seg->pointIndex + 1];
    const AssetFontPoint end   = font->points[seg->pointIndex + 2];
    const f32            res   = font_math_quad_bezier_dist(start, ctrl, end, point);
    return math_abs(res);
  }
  }
  diag_crash();
}

f32 asset_font_glyph_dist(
    const AssetFontComp* font, const AssetFontGlyph* glyph, const AssetFontPoint point) {

  f32 res = f32_max;
  for (usize seg = glyph->segmentIndex; seg != glyph->segmentIndex + glyph->segmentCount; ++seg) {
    const f32 dist = asset_font_seg_dist(font, seg, point);
    if (dist < res) {
      res = dist;
    }
  }
  return res;
}
