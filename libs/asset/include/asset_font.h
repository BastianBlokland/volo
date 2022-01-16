#pragma once
#include "core_unicode.h"
#include "ecs_module.h"

typedef struct {
  UnicodeCp cp;
  u32       glyphIndex;
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
  f32 size;
  f32 offsetX, offsetY;
} AssetFontGlyph;

ecs_comp_extern_public(AssetFontComp) {
  AssetFontChar*    characters; // Sorted on the unicode codepoint.
  usize             characterCount;
  AssetFontPoint*   points;
  usize             pointCount;
  AssetFontSegment* segments;
  usize             segmentCount;
  AssetFontGlyph*   glyphs;
  usize             glyphCount;
};

/**
 * Get a glyph based on an unicode codepoint.
 */
const AssetFontGlyph* asset_font_lookup(const AssetFontComp*, UnicodeCp);

/**
 * Get a set of glyphs to represent the input utf8 string.
 * Return the amount glyphs written to the output.
 * NOTE: Provide 'null' as the out pointer to query the amount of glyphs without writing them.
 */
usize asset_font_lookup_utf8(
    const AssetFontComp*, String, const AssetFontGlyph** out, usize outCount);

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
