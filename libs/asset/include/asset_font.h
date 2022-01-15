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
  struct {
    AssetFontCodepoint* values; // Sorted on the unicode value.
    usize               count;
  } codepoints;
  struct {
    AssetFontPoint* values;
    usize           count;
  } points;
  struct {
    AssetFontSegment* values;
    usize             count;
  } segments;
  struct {
    AssetFontGlyph* values;
    usize           count;
  } glyphs;
};

/**
 * Compare two AssetFontCodepoint's.
 * Signature is compatible with the 'CompareFunc' from 'core_compare.h'.
 */
i8 asset_font_compare_codepoint(const void* a, const void* b);
