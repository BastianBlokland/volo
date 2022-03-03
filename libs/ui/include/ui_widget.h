#pragma once
#include "ecs_module.h"
#include "ui_color.h"

// Forward declare from 'ui_canvas.h'.
ecs_comp_extern(UiCanvasComp);

typedef struct {
  String  label;
  u16     fontSize;
  UiColor frameColor;
} UiWidgetButtonOpts;

// clang-format off

/**
 * Draw a button in the currently active canvas rectangle.
 * Returns true if the button was activated, otherwise false.
 * NOTE: Its important that the button has a stable identifier in the canvas.
 */
#define ui_button(_CANVAS_, ...) ui_widget_button((_CANVAS_), &((UiWidgetButtonOpts){              \
  .fontSize   = 25,                                                                                \
  .frameColor = ui_color(32, 32, 32, 192),                                                         \
  __VA_ARGS__}))

// clang-format on

bool ui_widget_button(UiCanvasComp*, const UiWidgetButtonOpts*);
