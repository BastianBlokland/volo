#pragma once
#include "ecs_module.h"

typedef struct {
  u32 unicode;
  u32 glyphIndex;
} AssetFontCodepoint;

typedef union {
  struct {
    f32 x, y;
  };
  f32 comps[2];
} AssetFontPoint;

typedef enum {
  AssetFontSegment_Line,            // Consists of to 2 points, begin and end.
  AssetFontSegment_QuadraticBezier, // Consists of 3 points, begin, control, end.
} AssetFontSegmentType;

typedef struct {
  AssetFontSegmentType type;
  u32                  pointIndex; // Index of the first point, number of points depends on type.
} AssetFontSegment;

typedef struct {
  u32 segmentIndex;
  u32 segmentCount;
} AssetFontGlyph;

ecs_comp_extern_public(AssetFontComp) {
  AssetFontCodepoint* codepoints; // Sorted on the unicode value.
  usize               codepointCount;
  AssetFontPoint*     points;
  usize               pointCount;
  AssetFontSegment*   segments;
  usize               segmentCount;
  AssetFontGlyph*     glyphs;
  usize               glyphCount;
};

/**
 * Find a glyph based on an unicode value.
 */
const AssetFontGlyph* asset_font_lookup_unicode(const AssetFontComp*, u32 unicode);
