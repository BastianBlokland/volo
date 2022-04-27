#include "asset_manager.h"
#include "debug_asset.h"
#include "ecs_world.h"
#include "ui.h"

ecs_comp_define(DebugAssetPanelComp) { UiPanel panel; };

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugAssetPanelComp);
  ecs_access_write(UiCanvasComp);
}

static void asset_panel_draw(UiCanvasComp* canvas, DebugAssetPanelComp* panelComp) {
  const String title = fmt_write_scratch("{} Asset Debug", fmt_ui_shape(Storage));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugAssetUpdatePanelSys) {
  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId    entity    = ecs_view_entity(itr);
    DebugAssetPanelComp* panelComp = ecs_view_write_t(itr, DebugAssetPanelComp);
    UiCanvasComp*        canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    asset_panel_draw(canvas, panelComp);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_asset_module) {
  ecs_register_comp(DebugAssetPanelComp);

  ecs_register_view(PanelUpdateView);

  ecs_register_system(DebugAssetUpdatePanelSys, ecs_view_id(PanelUpdateView));
}

EcsEntityId debug_asset_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(world, panelEntity, DebugAssetPanelComp, .panel = ui_panel(ui_vector(330, 220)));
  return panelEntity;
}
