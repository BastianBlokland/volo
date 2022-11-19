#include "asset_prefab.h"
#include "core_format.h"
#include "core_stringtable.h"
#include "debug_prefab.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_prefab.h"
#include "ui.h"

ecs_comp_define(DebugPrefabPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
};

ecs_view_define(PrefabMapView) { ecs_access_read(AssetPrefabMapComp); }

static void prefab_panel_options_draw(UiCanvasComp* canvas) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(5, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);

  ui_table_next_row(canvas, &table);
  ui_layout_move_dir(canvas, Ui_Right, 5, UiBase_Absolute);
  ui_label(canvas, string_lit("Test"));
  ui_table_next_column(canvas, &table);

  ui_layout_pop(canvas);
}

static void prefab_panel_draw(
    UiCanvasComp* canvas, DebugPrefabPanelComp* panelComp, const AssetPrefabMapComp* prefabMap) {

  const String title = fmt_write_scratch("{} Prefab Panel", fmt_ui_shape(Construction));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  prefab_panel_options_draw(canvas);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 300);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Name"), string_lit("Prefab name.")},
          {string_lit("Actions"), string_lit("Prefab actions.")},
      });

  const f32 totalHeight = ui_table_height(&table, (u32)prefabMap->prefabCount);
  ui_scrollview_begin(canvas, &panelComp->scrollview, totalHeight);

  for (u32 prefabIdx = 0; prefabIdx != prefabMap->prefabCount; ++prefabIdx) {
    AssetPrefab* prefab = &prefabMap->prefabs[prefabIdx];
    const String name   = stringtable_lookup(g_stringtable, prefab->nameHash);

    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));

    ui_label(canvas, name);
    ui_table_next_column(canvas, &table);
  }

  ui_scrollview_end(canvas, &panelComp->scrollview);

  ui_layout_container_pop(canvas);
  ui_panel_end(canvas, &panelComp->panel);
}

ecs_view_define(PanelUpdateGlobalView) { ecs_access_read(ScenePrefabResourceComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugPrefabPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DebugPrefabUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const ScenePrefabResourceComp* prefabRes = ecs_view_read_t(globalItr, ScenePrefabResourceComp);

  EcsView*     mapView = ecs_world_view_t(world, PrefabMapView);
  EcsIterator* mapItr  = ecs_view_maybe_at(mapView, scene_prefab_map(prefabRes));
  if (!mapItr) {
    return;
  }
  const AssetPrefabMapComp* prefabMap = ecs_view_read_t(mapItr, AssetPrefabMapComp);

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugPrefabPanelComp* panelComp = ecs_view_write_t(itr, DebugPrefabPanelComp);
    UiCanvasComp*         canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    prefab_panel_draw(canvas, panelComp, prefabMap);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_prefab_module) {
  ecs_register_comp(DebugPrefabPanelComp);

  ecs_register_view(PrefabMapView);
  ecs_register_view(PanelUpdateGlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugPrefabUpdatePanelSys,
      ecs_view_id(PrefabMapView),
      ecs_view_id(PanelUpdateGlobalView),
      ecs_view_id(PanelUpdateView));
}

EcsEntityId debug_prefab_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugPrefabPanelComp,
      .panel = ui_panel(.position = ui_vector(0.2f, 0.5f), .size = ui_vector(500, 550)));
  return panelEntity;
}
