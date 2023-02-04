#pragma once
#include "core_annotation.h"
#include "core_types.h"

typedef union uGeoColor {
  struct {
    f32 r, g, b, a;
  };
  f32 data[4];
} GeoColor;

ASSERT(sizeof(GeoColor) == 16, "GeoColor has to be 128 bits");

/**
 * Construct a new color.
 */
#define geo_color(_R_, _G_, _B_, _A_) ((GeoColor){.r = (_R_), .g = (_G_), .b = (_B_), .a = (_A_)})

// clang-format off

/**
 * Color presets.
 */
#define geo_color_white           geo_color(1.0f, 1.0f, 1.0f, 1.0f)
#define geo_color_black           geo_color(0.0f, 0.0f, 0.0f, 1.0f)
#define geo_color_clear           geo_color(0.0f, 0.0f, 0.0f, 0.0f)
#define geo_color_silver          geo_color(0.75f, 0.75f, 0.75f, 1.0f)
#define geo_color_gray            geo_color(0.5f, 0.5f, 0.5f, 1.0f)
#define geo_color_red             geo_color(1.0f, 0.0f, 0.0f, 1.0f)
#define geo_color_maroon          geo_color(0.5f, 0.0f, 0.0f, 1.0f)
#define geo_color_yellow          geo_color(1.0f, 1.0f, 0.0f, 1.0f)
#define geo_color_olive           geo_color(0.5f, 0.5f, 0.0f, 1.0f)
#define geo_color_lime            geo_color(0.0f, 1.0f, 0.0f, 1.0f)
#define geo_color_green           geo_color(0.0f, 0.5f, 0.0f, 1.0f)
#define geo_color_aqua            geo_color(0.0f, 1.0f, 1.0f, 1.0f)
#define geo_color_teal            geo_color(0.0f, 0.5f, 0.5f, 1.0f)
#define geo_color_blue            geo_color(0.0f, 0.0f, 1.0f, 1.0f)
#define geo_color_navy            geo_color(0.0f, 0.0f, 0.5f, 1.0f)
#define geo_color_fuchsia         geo_color(1.0f, 0.0f, 1.0f, 1.0f)
#define geo_color_purple          geo_color(0.5f, 0.0f, 0.5f, 1.0f)
#define geo_color_orange          geo_color(1.0f, 0.5f, 0.0f, 1.0f)
#define geo_color_soothing_purple geo_color(0.188f, 0.039f, 0.141f, 1.0f)

// clang-format on

/**
 * Get a color from an index.
 * Useful for generating debug colors.
 */
GeoColor geo_color_get(u64 idx);

/**
 * Compute a color where each component is the result of adding the components of both colors.
 */
GeoColor geo_color_add(GeoColor a, GeoColor b);

/**
 * Compute a color where each component is the result of multiplying with the scalar.
 */
GeoColor geo_color_mul(GeoColor, f32 scalar);

/**
 * Compute a color where each component is the result of dividing with the scalar.
 */
GeoColor geo_color_div(GeoColor, f32 scalar);

/**
 * Calculate the linearly interpolated color from x to y at time t.
 * NOTE: Does not clamp t (so can extrapolate too).
 */
GeoColor geo_color_lerp(GeoColor x, GeoColor y, f32 t);

/**
 * Calculate the bilinearly interpolated color in the rectangle formed by v1, v2, v3 and v4.
 * More info: https://en.wikipedia.org/wiki/Bilinear_interpolation
 * NOTE: Does not clamp t (so can extrapolate too).
 */
GeoColor geo_color_bilerp(GeoColor v1, GeoColor v2, GeoColor v3, GeoColor v4, f32 tX, f32 tY);

/**
 * Pack a color to 16 bit floats.
 */
void geo_color_pack_f16(GeoColor, f16 out[4]);

/**
 * Create a formatting argument for a color.
 * NOTE: _COL_ is expanded multiple times, so care must be taken when providing complex expressions.
 */
#define geo_color_fmt(_COL_)                                                                       \
  fmt_list_lit(                                                                                    \
      fmt_float((_COL_).r), fmt_float((_COL_).g), fmt_float((_COL_).b), fmt_float((_COL_).a))
