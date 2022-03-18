#pragma once
#include "ecs_module.h"
#include "ui_rect.h"

ecs_comp_extern(UiCanvasComp);

typedef enum {
  UiPanelFlags_Center       = 1 << 0,
  UiPanelFlags_Close        = 1 << 1,
  UiPanelFlags_RequestFocus = 1 << 2,
  UiPanelFlags_Drawing      = 1 << 3,
} UiPanelFlags;

typedef struct {
  UiRect       rect;
  UiPanelFlags flags;
} UiPanelState;

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

UiPanelState ui_panel_init(UiVector size);
void         ui_panel_begin_with_opts(UiCanvasComp*, UiPanelState*, const UiPanelOpts*);
void         ui_panel_end(UiCanvasComp*, UiPanelState*);
