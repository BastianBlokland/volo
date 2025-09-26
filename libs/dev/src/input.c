#include "core/array.h"
#include "core/format.h"
#include "dev/panel.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "input/manager.h"
#include "ui/canvas.h"
#include "ui/panel.h"
#include "ui/scrollview.h"
#include "ui/shape.h"

typedef enum {
  DevInputTab_Platform,

  DevInputTab_Count,
} DevInputTab;

static const String g_inputTabNames[] = {
    string_static("Platform"),
};
ASSERT(array_elems(g_inputTabNames) == DevInputTab_Count, "Incorrect number of names");

ecs_comp_define(DevInputPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
};

static void platform_panel_tab_draw(
    UiCanvasComp* c, DevInputPanelComp* panelComp, const GapPlatformComp* platform) {
  (void)c;
  (void)panelComp;
  (void)platform;
}

static void input_panel_draw(
    UiCanvasComp*           c,
    DevInputPanelComp*      panelComp,
    const InputManagerComp* input,
    const GapPlatformComp*  platform) {
  (void)input;

  const String title = fmt_write_scratch("{} Input Panel", fmt_ui_shape(Keyboard));
  ui_panel_begin(
      c,
      &panelComp->panel,
      .title       = title,
      .tabNames    = g_inputTabNames,
      .tabCount    = DevInputTab_Count,
      .topBarColor = ui_color(100, 0, 0, 192));

  switch (panelComp->panel.activeTab) {
  case DevInputTab_Platform:
    platform_panel_tab_draw(c, panelComp, platform);
    break;
  }

  ui_panel_end(c, &panelComp->panel);
}

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(GapPlatformComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevInputPanelComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DevInputPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DevInputUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const InputManagerComp* input    = ecs_view_read_t(globalItr, InputManagerComp);
  const GapPlatformComp*  platform = ecs_view_read_t(globalItr, GapPlatformComp);

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);

  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DevInputPanelComp* panelComp = ecs_view_write_t(itr, DevInputPanelComp);
    UiCanvasComp*      canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp)) && !pinned) {
      continue;
    }
    input_panel_draw(canvas, panelComp, input, platform);

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(dev_input_module) {
  ecs_register_comp(DevInputPanelComp);

  ecs_register_system(
      DevInputUpdatePanelSys,
      ecs_register_view(PanelUpdateGlobalView),
      ecs_register_view(PanelUpdateView));
}

EcsEntityId
dev_input_panel_open(EcsWorld* world, const EcsEntityId window, const DevPanelType type) {
  const EcsEntityId  panelEntity = dev_panel_create(world, window, type);
  DevInputPanelComp* inputPanel  = ecs_world_add_t(
      world, panelEntity, DevInputPanelComp, .panel = ui_panel(.size = ui_vector(800, 600)));

  if (type == DevPanelType_Detached) {
    ui_panel_maximize(&inputPanel->panel);
  }

  return panelEntity;
}
