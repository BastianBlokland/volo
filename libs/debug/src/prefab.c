#include "asset_prefab.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_stringtable.h"
#include "debug_prefab.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "debug_stats.h"
#include "ecs_world.h"
#include "input_manager.h"
#include "scene_camera.h"
#include "scene_prefab.h"
#include "scene_selection.h"
#include "ui.h"

static const f32          g_createMinInteractDist = 1.0f;
static const f32          g_createMaxInteractDist = 250.0f;
static const InputBlocker g_createInputBlockers =
    InputBlocker_HoveringUi | InputBlocker_HoveringGizmo | InputBlocker_TextInput |
    InputBlocker_CursorLocked;

typedef enum {
  PrefabPanelMode_Normal,
  PrefabPanelMode_Create,

  PrefabPanelMode_Count,
} PrefabPanelMode;

static const String g_prefabPanelModeNames[] = {
    [PrefabPanelMode_Normal] = string_static("Normal"),
    [PrefabPanelMode_Create] = string_static("Create"),
};
ASSERT(array_elems(g_prefabPanelModeNames) == PrefabPanelMode_Count, "Missing mode name");

ecs_comp_define(DebugPrefabPanelComp) {
  PrefabPanelMode mode;
  StringHash      createPrefabId;
  SceneFaction    createFaction;
  UiPanel         panel;
  UiScrollview    scrollview;
};

typedef struct {
  EcsWorld*                 world;
  const AssetPrefabMapComp* prefabMap;
  const InputManagerComp*   input;
  DebugPrefabPanelComp*     panelComp;
  DebugShapeComp*           shape;
  DebugStatsGlobalComp*     globalStats;
  SceneSelectionComp*       selection;
} PrefabPanelContext;

ecs_view_define(PrefabMapView) { ecs_access_read(AssetPrefabMapComp); }
ecs_view_define(PrefabInstanceView) { ecs_access_read(ScenePrefabInstanceComp); }
ecs_view_define(CameraView) {
  ecs_access_read(SceneCameraComp);
  ecs_access_read(SceneTransformComp);
}

static u32* prefab_instance_counts_scratch(const PrefabPanelContext* ctx) {
  Mem mem = alloc_alloc(g_alloc_scratch, ctx->prefabMap->prefabCount * sizeof(u32), alignof(u32));
  mem_set(mem, 0);

  u32* res = mem.ptr;

  EcsView* prefabInstanceView = ecs_world_view_t(ctx->world, PrefabInstanceView);
  for (EcsIterator* itr = ecs_view_itr(prefabInstanceView); ecs_view_walk(itr);) {
    const ScenePrefabInstanceComp* instComp = ecs_view_read_t(itr, ScenePrefabInstanceComp);

    const u32 prefabIndex = asset_prefab_get_index(ctx->prefabMap, instComp->prefabId);
    diag_assert(!sentinel_check(prefabIndex));

    ++res[prefabIndex];
  }
  return res;
}

static void prefab_destroy_all(const PrefabPanelContext* ctx, const StringHash prefabId) {
  EcsView* prefabInstanceView = ecs_world_view_t(ctx->world, PrefabInstanceView);
  for (EcsIterator* itr = ecs_view_itr(prefabInstanceView); ecs_view_walk(itr);) {
    const ScenePrefabInstanceComp* instComp = ecs_view_read_t(itr, ScenePrefabInstanceComp);

    if (instComp->prefabId == prefabId) {
      ecs_world_entity_destroy(ctx->world, ecs_view_entity(itr));
    }
  }
}

static void prefab_select_all(const PrefabPanelContext* ctx, const StringHash prefabId) {
  scene_selection_clear(ctx->selection);

  EcsView* prefabInstanceView = ecs_world_view_t(ctx->world, PrefabInstanceView);
  for (EcsIterator* itr = ecs_view_itr(prefabInstanceView); ecs_view_walk(itr);) {
    const ScenePrefabInstanceComp* instComp = ecs_view_read_t(itr, ScenePrefabInstanceComp);

    if (instComp->prefabId == prefabId) {
      scene_selection_add(ctx->selection, ecs_view_entity(itr));
    }
  }
}

static void prefab_mode_change(const PrefabPanelContext* ctx, const PrefabPanelMode mode) {
  ctx->panelComp->mode = mode;
  debug_stats_notify(ctx->globalStats, string_lit("Prefab mode"), g_prefabPanelModeNames[mode]);
}

static void prefab_create_start(const PrefabPanelContext* ctx, const StringHash prefabId) {
  prefab_mode_change(ctx, PrefabPanelMode_Create);
  ctx->panelComp->createPrefabId = prefabId;
}

static void prefab_create_cancel(const PrefabPanelContext* ctx) {
  prefab_mode_change(ctx, PrefabPanelMode_Normal);
}

static void prefab_create_accept(const PrefabPanelContext* ctx, const GeoVector pos) {
  scene_prefab_spawn(
      ctx->world,
      &(ScenePrefabSpec){
          .prefabId = ctx->panelComp->createPrefabId,
          .position = pos,
          .rotation = geo_quat_ident,
          .faction  = ctx->panelComp->createFaction,
      });
  prefab_mode_change(ctx, PrefabPanelMode_Normal);
}

static void prefab_create_update(const PrefabPanelContext* ctx) {
  diag_assert(ctx->panelComp->mode == PrefabPanelMode_Create);
  diag_assert(ctx->panelComp->createPrefabId);

  EcsView* cameraView = ecs_world_view_t(ctx->world, CameraView);
  if (!ecs_view_contains(cameraView, input_active_window(ctx->input))) {
    prefab_create_cancel(ctx); // No active window.
    return;
  }

  EcsIterator*              camItr      = ecs_view_at(cameraView, input_active_window(ctx->input));
  const SceneCameraComp*    camera      = ecs_view_read_t(camItr, SceneCameraComp);
  const SceneTransformComp* cameraTrans = ecs_view_read_t(camItr, SceneTransformComp);

  const bool blocked = (input_blockers(ctx->input) & g_createInputBlockers) != 0;
  const bool create  = !blocked && input_triggered_lit(ctx->input, "DebugPrefabCreate");

  const GeoVector inputNormPos = geo_vector(input_cursor_x(ctx->input), input_cursor_y(ctx->input));
  const f32       inputAspect  = input_cursor_aspect(ctx->input);
  const GeoRay    inputRay     = scene_camera_ray(camera, cameraTrans, inputAspect, inputNormPos);
  const GeoPlane  groundPlane  = {.normal = geo_up};

  const f32 rayT = geo_plane_intersect_ray(&groundPlane, &inputRay);
  if (rayT > g_createMinInteractDist && rayT < g_createMaxInteractDist) {
    const GeoVector pos = geo_ray_position(&inputRay, rayT);
    if (!blocked) {
      debug_sphere(ctx->shape, pos, 0.5f, geo_color_green, DebugShape_Overlay);

      debug_stats_notify(
          ctx->globalStats,
          string_lit("Prefab location"),
          fmt_write_scratch(
              "x: {<5} z: {<5}",
              fmt_float(pos.x, .minDecDigits = 1, .maxDecDigits = 1, .expThresholdNeg = 0),
              fmt_float(pos.z, .minDecDigits = 1, .maxDecDigits = 1, .expThresholdNeg = 0)));
    }
    if (create) {
      prefab_create_accept(ctx, pos);
      return; // Prefab created.
    }
  }

  if (create) {
    prefab_create_cancel(ctx);
    return; // No position found to create the prefab.
  }
}

static void prefab_panel_options_normal_draw(UiCanvasComp* canvas, const PrefabPanelContext* ctx) {
  (void)ctx;
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

static void prefab_panel_options_create_draw(UiCanvasComp* canvas, const PrefabPanelContext* ctx) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(5, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_layout_move_dir(canvas, Ui_Right, 5, UiBase_Absolute);
  ui_label(canvas, string_lit("Creating prefab"));
  ui_table_next_column(canvas, &table);

  if (ui_button(canvas, .label = string_lit("Cancel"), .frameColor = ui_color(255, 16, 0, 192))) {
    prefab_create_cancel(ctx);
  }
  ui_layout_pop(canvas);
}

static void prefab_panel_options_draw(UiCanvasComp* canvas, const PrefabPanelContext* ctx) {
  switch (ctx->panelComp->mode) {
  case PrefabPanelMode_Normal:
    prefab_panel_options_normal_draw(canvas, ctx);
    break;
  case PrefabPanelMode_Create:
    prefab_panel_options_create_draw(canvas, ctx);
    break;
  case PrefabPanelMode_Count:
    break;
  }
  ui_canvas_id_block_next(canvas);
}

static void prefab_panel_draw(UiCanvasComp* canvas, const PrefabPanelContext* ctx) {

  const String title = fmt_write_scratch("{} Prefab Panel", fmt_ui_shape(Construction));
  ui_panel_begin(canvas, &ctx->panelComp->panel, .title = title);

  prefab_panel_options_draw(canvas, ctx);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  const bool disabled = ctx->panelComp->mode != PrefabPanelMode_Normal;

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

  const u32* instanceCounts = prefab_instance_counts_scratch(ctx);

  const f32 totalHeight = ui_table_height(&table, (u32)ctx->prefabMap->prefabCount);
  ui_scrollview_begin(canvas, &ctx->panelComp->scrollview, totalHeight);

  for (u32 prefabIdx = 0; prefabIdx != ctx->prefabMap->prefabCount; ++prefabIdx) {
    AssetPrefab* prefab = &ctx->prefabMap->prefabs[prefabIdx];
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
      prefab_destroy_all(ctx, prefab->nameHash);
    }
    ui_layout_next(canvas, Ui_Right, 10);
    if (ui_button(
            canvas,
            .flags      = disabled ? UiWidget_Disabled : 0,
            .label      = ui_shape_scratch(UiShape_SelectAll),
            .fontSize   = 18,
            .frameColor = disabled ? ui_color(64, 64, 64, 192) : ui_color(0, 16, 255, 192),
            .tooltip    = string_lit("Select all instances."))) {
      prefab_select_all(ctx, prefab->nameHash);
    }
    ui_layout_next(canvas, Ui_Right, 10);
    if (ui_button(
            canvas,
            .flags      = disabled ? UiWidget_Disabled : 0,
            .label      = ui_shape_scratch(UiShape_Add),
            .fontSize   = 18,
            .frameColor = disabled ? ui_color(64, 64, 64, 192) : ui_color(16, 192, 0, 192),
            .tooltip    = string_lit("Create a new instance."))) {
      prefab_create_start(ctx, prefab->nameHash);
    }
  }

  ui_scrollview_end(canvas, &ctx->panelComp->scrollview);

  ui_style_pop(canvas);
  ui_layout_container_pop(canvas);
  ui_panel_end(canvas, &ctx->panelComp->panel);
}

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(ScenePrefabResourceComp);
  ecs_access_write(DebugShapeComp);
  ecs_access_write(DebugStatsGlobalComp);
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

    const PrefabPanelContext ctx = {
        .world       = world,
        .prefabMap   = prefabMap,
        .input       = ecs_view_read_t(globalItr, InputManagerComp),
        .panelComp   = panelComp,
        .shape       = ecs_view_write_t(globalItr, DebugShapeComp),
        .globalStats = ecs_view_write_t(globalItr, DebugStatsGlobalComp),
        .selection   = ecs_view_write_t(globalItr, SceneSelectionComp),
    };
    switch (panelComp->mode) {
    case PrefabPanelMode_Normal:
      break;
    case PrefabPanelMode_Create:
      prefab_create_update(&ctx);
      break;
    case PrefabPanelMode_Count:
      break;
    }
    prefab_panel_draw(canvas, &ctx);

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
  ecs_register_view(CameraView);
  ecs_register_view(PanelUpdateGlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugPrefabUpdatePanelSys,
      ecs_view_id(PrefabMapView),
      ecs_view_id(PrefabInstanceView),
      ecs_view_id(CameraView),
      ecs_view_id(PanelUpdateGlobalView),
      ecs_view_id(PanelUpdateView));
}

EcsEntityId debug_prefab_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugPrefabPanelComp,
      .mode          = PrefabPanelMode_Normal,
      .createFaction = SceneFaction_None,
      .panel         = ui_panel(.position = ui_vector(0.2f, 0.3f), .size = ui_vector(500, 350)));
  return panelEntity;
}
