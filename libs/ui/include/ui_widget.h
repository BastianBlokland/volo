#pragma once
#include "ecs_module.h"
#include "ui_color.h"
#include "ui_rect.h"
#include "ui_units.h"

// Forward declare from 'ui_canvas.h'.
typedef u64 UiId;

ecs_comp_extern(UiCanvasComp);

typedef enum {
  UiWidget_Disabled = 1 << 0,
} UiWidgetFlags;

typedef struct {
  u16     fontSize;
  UiAlign align;
} UiLabelOpts;

typedef struct {
  UiWidgetFlags flags;
  String        label;
  u16           fontSize;
  UiColor       frameColor;
  String        tooltip;
} UiButtonOpts;

typedef struct {
  UiWidgetFlags flags;
  f32           min, max;
  f32           barHeight;
  f32           handleSize;
  f32           step;
  UiColor       barColor;
  String        tooltip;
} UiSliderOpts;

typedef struct {
  f32     size;
  UiColor bgColor;
  String  tooltip;
} UiToggleOpts;

typedef struct {
  u16      fontSize;
  UiVector maxSize;
} UiTooltipOpts;

// clang-format off

/**
 * Draw a label in the currently active canvas rectangle.
 */
#define ui_label(_CANVAS_, _TEXT_, ...) ui_label_with_opts((_CANVAS_), (_TEXT_),                   \
  &((UiLabelOpts){                                                                                 \
    .fontSize = 16,                                                                                \
    .align    = UiAlign_MiddleLeft,                                                                \
    __VA_ARGS__}))

/**
 * Draw a button in the currently active canvas rectangle.
 * Returns true if the button was activated, otherwise false.
 * NOTE: Its important that the widget has a stable identifier in the canvas.
 */
#define ui_button(_CANVAS_, ...) ui_button_with_opts((_CANVAS_),                                   \
  &((UiButtonOpts){                                                                                \
    .fontSize   = 16,                                                                              \
    .frameColor = ui_color(32, 32, 32, 192),                                                       \
    __VA_ARGS__}))

/**
 * Draw a slider in the currently active canvas rectangle.
 * Input value for the slider is updated to the given f32 pointer.
 * Return true if the widget is currently being used.
 * NOTE: Its important that the widget has a stable identifier in the canvas.
 */
#define ui_slider(_CANVAS_, _VALUE_, ...) ui_slider_with_opts((_CANVAS_), (_VALUE_),               \
  &((UiSliderOpts){                                                                                \
    .max        = 1,                                                                               \
    .barHeight  = 9,                                                                               \
    .handleSize = 20,                                                                              \
    .barColor   = ui_color(32, 32, 32, 192),                                                       \
    __VA_ARGS__}))

/**
 * Draw a toggle in the currently active canvas rectangle.
 * Input value for the toggle is updated to the given bool pointer.
 * NOTE: Its important that the widget has a stable identifier in the canvas.
 */
#define ui_toggle(_CANVAS_, _VALUE_, ...) ui_toggle_with_opts((_CANVAS_), (_VALUE_),               \
  &((UiToggleOpts){                                                                                \
    .size    = 20,                                                                                 \
    .bgColor = ui_color(32, 32, 32, 192),                                                          \
    __VA_ARGS__}))

/**
 * Draw a tooltip if the given element is being hovered.
 */
#define ui_tooltip(_CANVAS_, _ID_, _TEXT_, ...) ui_tooltip_with_opts((_CANVAS_), (_ID_), (_TEXT_), \
  &((UiTooltipOpts){                                                                               \
    .fontSize = 15,                                                                                \
    .maxSize  = {400, 400},                                                                        \
    __VA_ARGS__}))

// clang-format on

void ui_label_with_opts(UiCanvasComp*, String text, const UiLabelOpts*);
bool ui_button_with_opts(UiCanvasComp*, const UiButtonOpts*);
bool ui_slider_with_opts(UiCanvasComp*, f32* value, const UiSliderOpts*);
bool ui_toggle_with_opts(UiCanvasComp*, bool* value, const UiToggleOpts*);
bool ui_tooltip_with_opts(UiCanvasComp*, UiId, String text, const UiTooltipOpts*);
