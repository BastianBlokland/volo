#include "core_alloc.h"
#include "core_diag.h"
#include "debug_register.h"
#include "ecs_world.h"
#include "scene_level.h"
#include "ui.h"

static const String g_tooltipSave = string_static("Save the current scene as a level asset.");

ecs_comp_define(DebugLevelPanelComp) { UiPanel panel; };

static void
level_panel_draw(EcsWorld* world, UiCanvasComp* canvas, DebugLevelPanelComp* panelComp) {
  const String title = fmt_write_scratch("{} Level Panel", fmt_ui_shape(Globe));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 125);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Save scene"));
  ui_table_next_column(canvas, &table);
  if (ui_button(canvas, .label = string_lit("Save"), .tooltip = g_tooltipSave)) {
    scene_level_save(world, string_lit("test.lvl"));
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugLevelPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DebugLevelUpdatePanelSys) {
  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugLevelPanelComp* panelComp = ecs_view_write_t(itr, DebugLevelPanelComp);
    UiCanvasComp*        canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    level_panel_draw(world, canvas, panelComp);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_level_module) {
  ecs_register_comp(DebugLevelPanelComp);

  ecs_register_view(PanelUpdateView);

  ecs_register_system(DebugLevelUpdatePanelSys, ecs_view_id(PanelUpdateView));
}

EcsEntityId debug_level_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugLevelPanelComp,
      .panel = ui_panel(.position = ui_vector(0.75f, 0.5f), .size = ui_vector(375, 250)));
  return panelEntity;
}
