#pragma once
#include "ecs.h"
#include "ui.h"
#include "ui_color.h"
#include "ui_units.h"
#include "ui_vector.h"

typedef enum eUiWidgetFlags {
  UiWidget_Default           = 0,
  UiWidget_Disabled          = 1 << 0,
  UiWidget_DirtyWhileEditing = 1 << 1, // Always dirty during edit even if no changes occurred.
} UiWidgetFlags;

typedef enum {
  UiTextbox_Normal, // Entire sentence.
  UiTextbox_Word,   // Single word only.
  UiTextbox_Digits, // Digits only.
} UiTextboxType;

typedef struct {
  u16      fontSize;
  UiAlign  align;
  bool     selectable; // NOTE: Only supports single-line text.
  String   tooltip;
  UiVector tooltipMaxSize;
} UiLabelOpts;

typedef struct {
  UiWidgetFlags flags;
  bool          activate; // Force activate the button, useful for hotkeys for example.
  bool          noFrame;
  String        label;
  u16           fontSize;
  UiColor       frameColor;
  String        tooltip;
} UiButtonOpts;

typedef struct {
  UiWidgetFlags flags;
  bool          vertical;
  f32           min, max;
  f32           thickness;
  f32           handleSize;
  f32           step;
  UiColor       barColor;
  String        tooltip;
} UiSliderOpts;

typedef struct {
  UiWidgetFlags flags;
  f32           size;
  UiColor       bgColor;
  String        tooltip;
} UiToggleOpts;

typedef struct {
  UiWidgetFlags flags : 8;
  u16           fontSize;
  f32           maxHeight;
  UiColor       frameColor, dropFrameColor;
  String        placeholder;
  String        tooltip;
} UiSelectOpts;

typedef struct {
  UiWidgetFlags flags;
  u16           fontSize;
  UiVector      maxSize;
  u8            variation;
} UiTooltipOpts;

typedef struct {
  String label;
  u16    fontSize;
} UiSectionOpts;

typedef struct {
  UiWidgetFlags flags;
  UiTextboxType type;
  u16           fontSize;
  usize         maxTextLength;
  UiColor       frameColor;
  String        placeholder;
  String        tooltip;
} UiTextboxOpts;

typedef struct {
  UiWidgetFlags flags;
  f64           min, max;
  f64           step;
  u16           fontSize;
  UiColor       frameColor;
  String        tooltip;
} UiNumboxOpts;

typedef struct {
  UiWidgetFlags flags;
  TimeDuration  min, max;
  u16           fontSize;
  UiColor       frameColor;
  String        tooltip;
} UiDurboxOpts;

typedef struct {
  UiBase base;
  f32    radius;
  u16    maxCorner;
} UiCircleOpts;

typedef struct {
  UiBase base;
  f32    width;
} UiLineOpts;

// clang-format off

/**
 * Draw a label in the currently active canvas rectangle.
 */
#define ui_label(_CANVAS_, _TEXT_, ...) ui_label_with_opts((_CANVAS_), (_TEXT_),                   \
  &((UiLabelOpts){                                                                                 \
    .fontSize        = 16,                                                                         \
    .align           = UiAlign_MiddleLeft,                                                         \
    .tooltipMaxSize  = {500, 400},                                                                 \
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
    .thickness  = 9,                                                                               \
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
 * Draw a flag toggle in the currently active canvas rectangle.
 * Toggle the specified flag in the given u32 pointer's value.
 * NOTE: Its important that the widget has a stable identifier in the canvas.
 */
#define ui_toggle_flag(_CANVAS_, _VALUE_, _FLAG_, ...) ui_toggle_flag_with_opts(                   \
  (_CANVAS_), (_VALUE_), (_FLAG_), &((UiToggleOpts){                                                \
    .size    = 20,                                                                                 \
    .bgColor = ui_color(32, 32, 32, 192),                                                          \
    __VA_ARGS__}))

/**
 * Draw a select dropdown in the currently active canvas rectangle.
 * Input value for the selected item is updated to the given i32 pointer.
 * NOTE: Its important that the widget has a stable identifier in the canvas.
 */
#define ui_select(_CANVAS_, _VALUE_, _OPT_LABELS_, _OPT_COUNT_, ...)                               \
  ui_select_with_opts((_CANVAS_), (_VALUE_), (_OPT_LABELS_), (_OPT_COUNT_), &((UiSelectOpts){      \
    .fontSize       = 16,                                                                          \
    .maxHeight      = 150,                                                                         \
    .frameColor     = ui_color(32, 32, 32, 192),                                                   \
    .dropFrameColor = ui_color(64, 64, 64, 235),                                                   \
    .placeholder    = string_lit("< None >"),                                                      \
    __VA_ARGS__}))

/**
 * Draw a tooltip if the given element is being hovered.
 * NOTE: Provide sentinel_u64 as the id to force the tooltip to be displayed.
 */
#define ui_tooltip(_CANVAS_, _ID_, _TEXT_, ...) ui_tooltip_with_opts((_CANVAS_), (_ID_), (_TEXT_), \
  &((UiTooltipOpts){                                                                               \
    .fontSize = 15,                                                                                \
    .maxSize  = {500, 400},                                                                        \
    __VA_ARGS__}))

/**
 * Draw a collapsable section.
 */
#define ui_section(_CANVAS_, ...) ui_section_with_opts((_CANVAS_),                                 \
  &((UiSectionOpts){                                                                               \
    .fontSize = 15,                                                                                \
    __VA_ARGS__}))

/**
 * Draw editable text box.
 * NOTE: Its important that the widget has a stable identifier in the canvas.
 */
#define ui_textbox(_CANVAS_, _DYN_TEXT_, ...) ui_textbox_with_opts((_CANVAS_), (_DYN_TEXT_),       \
  &((UiTextboxOpts){                                                                               \
    .fontSize      = 16,                                                                           \
    .maxTextLength = usize_kibibyte,                                                               \
    .frameColor    = ui_color(32, 32, 32, 192),                                                    \
    .placeholder   = string_lit("..."),                                                            \
    __VA_ARGS__}))

/**
 * Draw editable number box.
 * NOTE: Its important that the widget has a stable identifier in the canvas.
 */
#define ui_numbox(_CANVAS_, _VALUE_, ...) ui_numbox_with_opts((_CANVAS_), (_VALUE_),               \
  &((UiNumboxOpts){                                                                                \
    .max        = f64_max,                                                                         \
    .fontSize   = 16,                                                                              \
    .frameColor = ui_color(32, 32, 32, 192),                                                       \
    __VA_ARGS__}))

/**
 * Draw editable time duration box.
 * NOTE: Its important that the widget has a stable identifier in the canvas.
 */
#define ui_durbox(_CANVAS_, _VALUE_, ...) ui_durbox_with_opts((_CANVAS_), (_VALUE_),               \
  &((UiDurboxOpts){                                                                                \
    .max        = i64_max,                                                                         \
    .fontSize   = 16,                                                                              \
    .frameColor = ui_color(32, 32, 32, 192),                                                       \
    __VA_ARGS__}))


/**
 * Draw a circle at the given point.
 */
#define ui_circle(_CANVAS_, _POS_, ...) ui_circle_with_opts((_CANVAS_), (_POS_),                   \
  &((UiCircleOpts){                                                                                \
    .base   = UiBase_Current,                                                                      \
    .radius = 10,                                                                                  \
    __VA_ARGS__}))

/**
 * Draw a line between two given points.
 */
#define ui_line(_CANVAS_, _FROM_, _TO_, ...) ui_line_with_opts((_CANVAS_), (_FROM_), (_TO_),       \
  &((UiLineOpts){                                                                                  \
    .base  = UiBase_Current,                                                                       \
    .width = 10,                                                                                   \
    __VA_ARGS__}))

// clang-format on

void ui_label_with_opts(UiCanvasComp*, String text, const UiLabelOpts*);
void ui_label_entity(UiCanvasComp*, EcsEntityId);
bool ui_button_with_opts(UiCanvasComp*, const UiButtonOpts*);
bool ui_slider_with_opts(UiCanvasComp*, f32* value, const UiSliderOpts*);
bool ui_toggle_with_opts(UiCanvasComp*, bool* value, const UiToggleOpts*);
bool ui_toggle_flag_with_opts(UiCanvasComp*, u32* value, u32 flag, const UiToggleOpts*);
bool ui_select_with_opts(
    UiCanvasComp*, i32* value, const String* options, u32 optionCount, const UiSelectOpts*);
bool ui_tooltip_with_opts(UiCanvasComp*, UiId, String text, const UiTooltipOpts*);
bool ui_section_with_opts(UiCanvasComp*, const UiSectionOpts*);
bool ui_textbox_with_opts(UiCanvasComp*, DynString*, const UiTextboxOpts*);
bool ui_numbox_with_opts(UiCanvasComp*, f64*, const UiNumboxOpts*);
bool ui_durbox_with_opts(UiCanvasComp*, TimeDuration*, const UiDurboxOpts*);
void ui_circle_with_opts(UiCanvasComp*, UiVector pos, const UiCircleOpts*);
void ui_line_with_opts(UiCanvasComp*, UiVector from, UiVector to, const UiLineOpts*);
