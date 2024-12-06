#pragma once
#include "ecs_module.h"
#include "ui_color.h"
#include "ui_rect.h"

ecs_comp_extern(UiCanvasComp);

typedef enum eUiPanelFlags {
  UiPanelFlags_Close     = 1 << 0,
  UiPanelFlags_Active    = 1 << 1,
  UiPanelFlags_Pinned    = 1 << 2,
  UiPanelFlags_Maximized = 1 << 3,
} UiPanelFlags;

typedef struct sUiPanel {
  UiVector     position; // In fractions of the canvas size.
  UiVector     size;     // In ui-pixels.
  UiVector     minSize;  // In ui-pixels.
  UiPanelFlags flags;
  u32          activeTab;
} UiPanel;

typedef struct {
  String        title;
  const String* tabNames;
  const u32     tabCount;
  UiColor       topBarColor;
  bool          pinnable;
} UiPanelOpts;

// clang-format off

/**
 * Create a layout panel.
 */
#define ui_panel(...) ((UiPanel){                                                                  \
  .position = ui_vector(0.5f, 0.5f),                                                               \
  .size     = ui_vector(300, 300),                                                                 \
  .minSize  = ui_vector(100, 100),                                                                 \
  __VA_ARGS__                                                                                      \
  })

/**
 * Draws a basic movable / resizable panel and sets an active container for drawing its contents.
 * NOTE: Should be followed by a 'ui_panel_end()'.
 * NOTE: Its important that the panel has a stable identifier in the canvas.
 */
#define ui_panel_begin(_CANVAS_, _PANEL_, ...) ui_panel_begin_with_opts((_CANVAS_), (_PANEL_),     \
  &((UiPanelOpts){                                                                                 \
    .topBarColor = ui_color(8, 8, 8, 240),                                                         \
    .pinnable    = true,                                                                           \
    __VA_ARGS__}))

// clang-format on

void ui_panel_begin_with_opts(UiCanvasComp*, UiPanel*, const UiPanelOpts*);
void ui_panel_end(UiCanvasComp*, UiPanel*);

void ui_panel_pin(UiPanel*);
void ui_panel_maximize(UiPanel*);
bool ui_panel_closed(const UiPanel*);
bool ui_panel_pinned(const UiPanel*);
bool ui_panel_maximized(const UiPanel*);
