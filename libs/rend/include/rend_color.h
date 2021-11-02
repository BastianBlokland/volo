#pragma once
#include "core_annotation.h"
#include "core_format.h"
#include "core_types.h"

typedef union {
  struct {
    float r, g, b, a;
  };
  float data[4];
} RendColor;

ASSERT(sizeof(RendColor) == 16, "RendColor has to be 128 bits");

/**
 * Construct a new color.
 */
#define rend_color(_R_, _G_, _B_, _A_) ((RendColor){.r = (_R_), .g = (_G_), .b = (_B_), .a = (_A_)})

/**
 * Color presets.
 */
#define rend_white rend_color(1.0f, 1.0f, 1.0f, 1.0f)
#define rend_black rend_color(0.0f, 0.0f, 0.0f, 1.0f)
#define rend_clear rend_color(0.0f, 0.0f, 0.0f, 0.0f)
#define rend_silver rend_color(0.75f, 0.75f, 0.75f, 1.0f)
#define rend_gray rend_color(0.5f, 0.5f, 0.5f, 1.0f)
#define rend_red rend_color(1.0f, 0.0f, 0.0f, 1.0f)
#define rend_maroon rend_color(0.5f, 0.0f, 0.0f, 1.0f)
#define rend_yellow rend_color(1.0f, 1.0f, 0.0f, 1.0f)
#define rend_olive rend_color(0.5f, 0.5f, 0.0f, 1.0f)
#define rend_lime rend_color(0.0f, 1.0f, 0.0f, 1.0f)
#define rend_green rend_color(0.0f, 0.5f, 0.0f, 1.0f)
#define rend_aqua rend_color(0.0f, 1.0f, 1.0f, 1.0f)
#define rend_teal rend_color(0.0f, 0.5f, 0.5f, 1.0f)
#define rend_blue rend_color(0.0f, 0.0f, 1.0f, 1.0f)
#define rend_navy rend_color(0.0f, 0.0f, 0.5f, 1.0f)
#define rend_fuchsia rend_color(1.0f, 0.0f, 1.0f, 1.0f)
#define rend_purple rend_color(0.5f, 0.0f, 0.5f, 1.0f)
#define rend_soothing_purple rend_color(0.188f, 0.039f, 0.141f, 1.0f)

/**
 * Get a color from an index.
 * Usefull for generating debug colors.
 */
RendColor rend_color_get(u64 idx);

/**
 * Create a formatting argument for a color.
 * NOTE: _COL_ is expanded multiple times, so care must be taken when providing complex expressions.
 */
#define rend_color_fmt(_COL_)                                                                      \
  fmt_list_lit(                                                                                    \
      fmt_float((_COL_).r), fmt_float((_COL_).g), fmt_float((_COL_).b), fmt_float((_COL_).a))
