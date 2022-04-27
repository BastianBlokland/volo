#pragma once
#include "ecs_module.h"

ecs_comp_extern(UiCanvasComp);

typedef enum {
  UiScrollviewFlags_Active = 1 << 0,
} UiScrollviewFlags;

typedef struct {
  UiScrollviewFlags flags;
  f32               offset;
} UiScrollview;

/**
 * Create a layout scrollview.
 */
#define ui_scrollview(...) ((UiScrollview){0, __VA_ARGS__})

/**
 * Begin a scrollview layout.
 * The provided height is the maximum height of the content to be rendered, the actual viewport into
 * the content is based on the current rectangle.
 * Pre-condition: height > 0.
 */
void ui_scrollview_begin(UiCanvasComp*, UiScrollview*, f32 height);
void ui_scrollview_end(UiCanvasComp*, UiScrollview*);
