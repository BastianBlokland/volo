#pragma once
#include "ecs_module.h"
#include "ui_rect.h"

ecs_comp_extern(UiCanvasComp);

typedef enum {
  UiPanelRequests_Center = 1 << 0,
} UiPanelRequests;

typedef enum {
  UiPanelEvents_Close   = 1 << 0,
  UiPanelEvents_ToFront = 1 << 1,
} UiPanelEvents;

typedef struct {
  UiPanelRequests requests;
  UiPanelEvents   events;
  UiRect          rect;
} UiPanel;

void ui_panel_begin(UiCanvasComp*, UiPanel*, String title);
void ui_panel_end(UiCanvasComp*, UiPanel*);
