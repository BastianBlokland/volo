#pragma once
#include "core_annotation.h"
#include "core_format.h"

/**
 * UI Color, 8 bits per channel.
 */
typedef union {
  struct {
    u8 r, g, b, a;
  };
  u32 data;
} UiColor;

ASSERT(sizeof(UiColor) == 4, "UiColor has to be 32 bits");

/**
 * Construct a new color.
 */
#define ui_color(_R_, _G_, _B_, _A_) ((UiColor){(_R_), (_G_), (_B_), (_A_)})

// clang-format off

/**
 * Color presets.
 */
#define ui_color_white    ui_color(255, 255, 255, 255)
#define ui_color_black    ui_color(0,   0,   0,   255)
#define ui_color_clear    ui_color(0,   0,   0,   0  )
#define ui_color_silver   ui_color(192, 192, 192, 255)
#define ui_color_gray     ui_color(128, 128, 128, 255)
#define ui_color_red      ui_color(255, 0,   0,   255)
#define ui_color_maroon   ui_color(128, 0,   0,   255)
#define ui_color_yellow   ui_color(255, 255, 0,   255)
#define ui_color_olive    ui_color(128, 128, 0,   255)
#define ui_color_lime     ui_color(0,   255, 0,   255)
#define ui_color_green    ui_color(0,   128, 0,   255)
#define ui_color_aqua     ui_color(0,   255, 255, 255)
#define ui_color_teal     ui_color(0,   128, 128, 255)
#define ui_color_blue     ui_color(0,   0,   255, 255)
#define ui_color_navy     ui_color(0,   0,   128, 255)
#define ui_color_fuchsia  ui_color(255, 0,   255, 255)
#define ui_color_purple   ui_color(128, 0,   128, 255)

// clang-format on

/**
 * Create a formatting argument for a color.
 */
#define ui_color_fmt(_VAL_) fmt_int((_VAL_).data, .base = 16, minDigits = 8)
