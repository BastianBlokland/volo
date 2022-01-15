#pragma once
#include "ecs_module.h"

typedef struct {
  u32 unicode;
  u32 glyphIndex;
} AssetFontChar;

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
  AssetFontChar*    characters; // Sorted on the unicode value.
  usize             characterCount;
  AssetFontPoint*   points;
  usize             pointCount;
  AssetFontSegment* segments;
  usize             segmentCount;
  AssetFontGlyph*   glyphs;
  usize             glyphCount;
};

/**
 * Find a glyph based on an unicode value.
 */
const AssetFontGlyph* asset_font_lookup_unicode(const AssetFontComp*, u32 unicode);

/**
 * Sample a position on the segment.
 * NOTE: t is a 'time' on the segment where 0 is the start and 1 is the end.
 * Pre-condition: index < font.segmentCount
 */
AssetFontPoint asset_font_seg_sample(const AssetFontComp*, usize index, f32 t);

/**
 * Calculate the length of a segment.
 * Pre-condition: index < font.segmentCount
 */
f32 asset_font_seg_length(const AssetFontComp* font, usize index);
