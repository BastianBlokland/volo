#pragma once
#include "ecs_module.h"
#include "ui_color.h"

// Forward declare from 'ui_canvas.h'.
ecs_comp_extern(UiCanvasComp);

typedef struct {
  String  label;
  u16     fontSize;
  UiColor frameColor;
} UiButtonOpts;

typedef struct {
  f32     min, max;
  f32     barHeight;
  f32     handleSize;
  UiColor barColor;
} UiSliderOpts;

// clang-format off

/**
 * Draw a button in the currently active canvas rectangle.
 * Returns true if the button was activated, otherwise false.
 * NOTE: Its important that the button has a stable identifier in the canvas.
 */
#define ui_button(_CANVAS_, ...) ui_button_with_opts((_CANVAS_),                                   \
  &((UiButtonOpts){                                                                                \
    .fontSize   = 25,                                                                              \
    .frameColor = ui_color(32, 32, 32, 192),                                                       \
    __VA_ARGS__}))

/**
 * Draw a slider in the currently active canvas rectangle.
 * Input value for the slider is updated to the given f32 pointer.
 * Return true if the widget is currently being used.
 * NOTE: Its important that the button has a stable identifier in the canvas.
 */
#define ui_slider(_CANVAS_, _VALUE_, ...) ui_slider_with_opts((_CANVAS_), (_VALUE_),               \
  &((UiSliderOpts){                                                                                \
    .min        = 0,                                                                               \
    .max        = 1,                                                                               \
    .barHeight  = 8,                                                                               \
    .handleSize = 20,                                                                              \
    .barColor   = ui_color(32, 32, 32, 192),                                                       \
    __VA_ARGS__}))

// clang-format on

bool ui_button_with_opts(UiCanvasComp*, const UiButtonOpts*);
bool ui_slider_with_opts(UiCanvasComp*, f32* value, const UiSliderOpts*);
