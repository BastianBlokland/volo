#pragma once
#include "ecs_module.h"
#include "ui_rect.h"

ecs_comp_extern(UiCanvasComp);

typedef enum {
  UiPanelFlags_Close   = 1 << 0,
  UiPanelFlags_ToFront = 1 << 1,
  UiPanelFlags_Drawing = 1 << 2,
} UiPanelFlags;

typedef struct {
  UiRect       rect;
  UiPanelFlags flags;
} UiPanel;

typedef struct {
  String title;
} UiPanelOpts;

// clang-format off

/**
 * Draws a basic movable panel and sets an active container for drawing its contents.
 * NOTE: Should be followed by a 'ui_panel_end()'.
 * NOTE: Its important that the panel has a stable identifier in the canvas.
 */
#define ui_panel_begin(_CANVAS_, _PANEL_, ...) ui_panel_begin_with_opts((_CANVAS_), (_PANEL_),     \
  &((UiPanelOpts){__VA_ARGS__}))

// clang-format on

void ui_panel_begin_with_opts(UiCanvasComp*, UiPanel*, const UiPanelOpts*);
void ui_panel_end(UiCanvasComp*, UiPanel*);
