#include "asset_prefab.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_format.h"
#include "core_stringtable.h"
#include "debug_prefab.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_prefab.h"
#include "scene_selection.h"
#include "ui.h"

typedef enum {
  DebugPrefabPanel_Normal,
  DebugPrefabPanel_CreateInstance,
} DebugPrefabPanelMode;

ecs_comp_define(DebugPrefabPanelComp) {
  UiPanel              panel;
  DebugPrefabPanelMode mode;
  UiScrollview         scrollview;
};

ecs_view_define(PrefabMapView) { ecs_access_read(AssetPrefabMapComp); }
ecs_view_define(PrefabInstanceView) { ecs_access_read(ScenePrefabInstanceComp); }

static u32* prefab_instance_counts_scratch(EcsWorld* world, const AssetPrefabMapComp* prefabMap) {
  Mem scratch = alloc_alloc(g_alloc_scratch, prefabMap->prefabCount * sizeof(u32), alignof(u32));
  mem_set(scratch, 0);

  u32* res = scratch.ptr;

  EcsView* prefabInstanceView = ecs_world_view_t(world, PrefabInstanceView);
  for (EcsIterator* itr = ecs_view_itr(prefabInstanceView); ecs_view_walk(itr);) {
    const ScenePrefabInstanceComp* instComp = ecs_view_read_t(itr, ScenePrefabInstanceComp);

    const u32 prefabIndex = asset_prefab_get_index(prefabMap, instComp->prefabId);
    diag_assert(!sentinel_check(prefabIndex));

    ++res[prefabIndex];
  }
  return res;
}

static void prefab_destroy_all(EcsWorld* world, const StringHash prefabId) {
  EcsView* prefabInstanceView = ecs_world_view_t(world, PrefabInstanceView);
  for (EcsIterator* itr = ecs_view_itr(prefabInstanceView); ecs_view_walk(itr);) {
    const ScenePrefabInstanceComp* instComp = ecs_view_read_t(itr, ScenePrefabInstanceComp);

    if (instComp->prefabId == prefabId) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
  }
}

static void prefab_select_all(EcsWorld* world, SceneSelectionComp* sel, const StringHash prefabId) {
  scene_selection_clear(sel);

  EcsView* prefabInstanceView = ecs_world_view_t(world, PrefabInstanceView);
  for (EcsIterator* itr = ecs_view_itr(prefabInstanceView); ecs_view_walk(itr);) {
    const ScenePrefabInstanceComp* instComp = ecs_view_read_t(itr, ScenePrefabInstanceComp);

    if (instComp->prefabId == prefabId) {
      scene_selection_add(sel, ecs_view_entity(itr));
    }
  }
}

static void prefab_create_instance_start(DebugPrefabPanelComp* panelComp) {
  panelComp->mode = DebugPrefabPanel_CreateInstance;
}

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
    EcsWorld*                 world,
    UiCanvasComp*             canvas,
    DebugPrefabPanelComp*     panelComp,
    const AssetPrefabMapComp* prefabMap,
    SceneSelectionComp*       selection) {

  const String title = fmt_write_scratch("{} Prefab Panel", fmt_ui_shape(Construction));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  prefab_panel_options_draw(canvas);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  const bool disabled = panelComp->mode != DebugPrefabPanel_Normal;

  ui_style_push(canvas);
  if (disabled) {
    ui_style_color_mult(canvas, 0.5f);
  }

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Name"), string_lit("Prefab name.")},
          {string_lit("Count"), string_lit("Amount of currently spawned instances.")},
          {string_lit("Actions"), string_empty},
      });

  const u32* instanceCounts = prefab_instance_counts_scratch(world, prefabMap);

  const f32 totalHeight = ui_table_height(&table, (u32)prefabMap->prefabCount);
  ui_scrollview_begin(canvas, &panelComp->scrollview, totalHeight);

  for (u32 prefabIdx = 0; prefabIdx != prefabMap->prefabCount; ++prefabIdx) {
    AssetPrefab* prefab = &prefabMap->prefabs[prefabIdx];
    const String name   = stringtable_lookup(g_stringtable, prefab->nameHash);

    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));

    const String nameTooltip = fmt_write_scratch(
        "Index: {}\nId (hash): {}", fmt_int(prefabIdx), fmt_int(string_hash(name), .base = 16));

    ui_label(canvas, name, .selectable = true, .tooltip = nameTooltip);
    ui_table_next_column(canvas, &table);

    ui_label(canvas, fmt_write_scratch("{}", fmt_int(instanceCounts[prefabIdx])));
    ui_table_next_column(canvas, &table);

    ui_layout_resize(canvas, UiAlign_MiddleLeft, ui_vector(25, 0), UiBase_Absolute, Ui_X);
    if (ui_button(
            canvas,
            .flags      = disabled ? UiWidget_Disabled : 0,
            .label      = ui_shape_scratch(UiShape_Delete),
            .fontSize   = 18,
            .frameColor = disabled ? ui_color(64, 64, 64, 192) : ui_color(255, 16, 0, 192),
            .tooltip    = string_lit("Destroy all instances."))) {
      prefab_destroy_all(world, prefab->nameHash);
    }
    ui_layout_next(canvas, Ui_Right, 10);
    if (ui_button(
            canvas,
            .flags      = disabled ? UiWidget_Disabled : 0,
            .label      = ui_shape_scratch(UiShape_SelectAll),
            .fontSize   = 18,
            .frameColor = disabled ? ui_color(64, 64, 64, 192) : ui_color(0, 16, 255, 192),
            .tooltip    = string_lit("Select all instances."))) {
      prefab_select_all(world, selection, prefab->nameHash);
    }
    ui_layout_next(canvas, Ui_Right, 10);
    if (ui_button(
            canvas,
            .flags      = disabled ? UiWidget_Disabled : 0,
            .label      = ui_shape_scratch(UiShape_Add),
            .fontSize   = 18,
            .frameColor = disabled ? ui_color(64, 64, 64, 192) : ui_color(16, 192, 0, 192),
            .tooltip    = string_lit("Create a new instance."))) {
      prefab_create_instance_start(panelComp);
    }
  }

  ui_scrollview_end(canvas, &panelComp->scrollview);

  ui_style_pop(canvas);
  ui_layout_container_pop(canvas);
  ui_panel_end(canvas, &panelComp->panel);
}

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_read(ScenePrefabResourceComp);
  ecs_access_write(SceneSelectionComp);
}

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
  SceneSelectionComp*            selection = ecs_view_write_t(globalItr, SceneSelectionComp);

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
    prefab_panel_draw(world, canvas, panelComp, prefabMap, selection);

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
  ecs_register_view(PrefabInstanceView);
  ecs_register_view(PanelUpdateGlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugPrefabUpdatePanelSys,
      ecs_view_id(PrefabMapView),
      ecs_view_id(PrefabInstanceView),
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
