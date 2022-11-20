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

// clang-format off

static const String       g_tooltipFilter         = string_static("Filter prefab's by identifier.\nSupports glob characters \a.b*\ar and \a.b?\ar.");
static const f32          g_createMinInteractDist = 1.0f;
static const f32          g_createMaxInteractDist = 250.0f;
static const InputBlocker g_createInputBlockers   = InputBlocker_HoveringUi | InputBlocker_HoveringGizmo | InputBlocker_TextInput | InputBlocker_CursorLocked;

// clang-format on

typedef enum {
  PrefabPanelMode_Normal,
  PrefabPanelMode_Create,

  PrefabPanelMode_Count,
} PrefabPanelMode;

ecs_comp_define(DebugPrefabPanelComp) {
  PrefabPanelMode mode;
  StringHash      createPrefabId;
  SceneFaction    createFaction;
  bool            createMultiple;
  DynString       idFilter;
  UiPanel         panel;
  UiScrollview    scrollview;
  u32             totalRows;
};

static void ecs_destruct_prefab_panel(void* data) {
  DebugPrefabPanelComp* comp = data;
  dynstring_destroy(&comp->idFilter);
}

typedef struct {
  EcsWorld*                 world;
  const AssetPrefabMapComp* prefabMap;
  DebugPrefabPanelComp*     panelComp;
  const InputManagerComp*   input;
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

static bool prefab_faction_select(UiCanvasComp* canvas, SceneFaction* faction) {
  static const String g_names[] = {
      string_static("None"),
      string_static("A"),
      string_static("B"),
      string_static("C"),
      string_static("D"),
  };
  static SceneFaction g_values[] = {
      SceneFaction_None,
      SceneFaction_A,
      SceneFaction_B,
      SceneFaction_C,
      SceneFaction_D,
  };
  ASSERT(array_elems(g_names) == array_elems(g_values), "Mismatching faction options");

  i32 index = 0;
  for (u32 i = 0; i != array_elems(g_values); ++i) {
    if (g_values[i] == *faction) {
      index = i;
      break;
    }
  }
  if (ui_select(canvas, &index, g_names, array_elems(g_values))) {
    *faction = g_values[index];
    return true;
  }
  return false;
}

static bool prefab_filter(const PrefabPanelContext* ctx, const String prefabName) {
  if (!ctx->panelComp->idFilter.size) {
    return true;
  }
  const String rawFilter = dynstring_view(&ctx->panelComp->idFilter);
  const String filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));
  return string_match_glob(prefabName, filter, StringMatchFlags_IgnoreCase);
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
  debug_stats_notify(ctx->globalStats, string_lit("Prefab action"), string_lit("Destroy all"));

  EcsView* prefabInstanceView = ecs_world_view_t(ctx->world, PrefabInstanceView);
  for (EcsIterator* itr = ecs_view_itr(prefabInstanceView); ecs_view_walk(itr);) {
    const ScenePrefabInstanceComp* instComp = ecs_view_read_t(itr, ScenePrefabInstanceComp);

    if (instComp->prefabId == prefabId) {
      ecs_world_entity_destroy(ctx->world, ecs_view_entity(itr));
    }
  }
}

static void prefab_select_all(const PrefabPanelContext* ctx, const StringHash prefabId) {
  debug_stats_notify(ctx->globalStats, string_lit("Prefab action"), string_lit("Select all"));

  scene_selection_clear(ctx->selection);

  EcsView* prefabInstanceView = ecs_world_view_t(ctx->world, PrefabInstanceView);
  for (EcsIterator* itr = ecs_view_itr(prefabInstanceView); ecs_view_walk(itr);) {
    const ScenePrefabInstanceComp* instComp = ecs_view_read_t(itr, ScenePrefabInstanceComp);

    if (instComp->prefabId == prefabId) {
      scene_selection_add(ctx->selection, ecs_view_entity(itr));
    }
  }
}

static void prefab_create_start(const PrefabPanelContext* ctx, const StringHash prefabId) {
  debug_stats_notify(ctx->globalStats, string_lit("Prefab action"), string_lit("Create start"));

  ctx->panelComp->mode           = PrefabPanelMode_Create;
  ctx->panelComp->createPrefabId = prefabId;
}

static void prefab_create_cancel(const PrefabPanelContext* ctx) {
  debug_stats_notify(ctx->globalStats, string_lit("Prefab action"), string_lit("Create cancel"));

  ctx->panelComp->mode = PrefabPanelMode_Normal;
}

static void prefab_create_accept(const PrefabPanelContext* ctx, const GeoVector pos) {
  debug_stats_notify(ctx->globalStats, string_lit("Prefab action"), string_lit("Create accept"));

  scene_prefab_spawn(
      ctx->world,
      &(ScenePrefabSpec){
          .prefabId = ctx->panelComp->createPrefabId,
          .position = pos,
          .rotation = geo_quat_ident,
          .faction  = ctx->panelComp->createFaction,
      });

  if (!ctx->panelComp->createMultiple) {
    ctx->panelComp->mode = PrefabPanelMode_Normal;
  }
}

static void prefab_create_update(const PrefabPanelContext* ctx) {
  diag_assert(ctx->panelComp->mode == PrefabPanelMode_Create);
  diag_assert(ctx->panelComp->createPrefabId);

  EcsView* cameraView = ecs_world_view_t(ctx->world, CameraView);
  if (!ecs_view_contains(cameraView, input_active_window(ctx->input))) {
    prefab_create_cancel(ctx); // No active window.
    return;
  }
  if (input_triggered_lit(ctx->input, "DebugPrefabCreateCancel")) {
    prefab_create_cancel(ctx); // Cancel requested.
    return;
  }

  EcsIterator*              camItr      = ecs_view_at(cameraView, input_active_window(ctx->input));
  const SceneCameraComp*    camera      = ecs_view_read_t(camItr, SceneCameraComp);
  const SceneTransformComp* cameraTrans = ecs_view_read_t(camItr, SceneTransformComp);

  const bool      blocked      = (input_blockers(ctx->input) & g_createInputBlockers) != 0;
  const GeoVector inputNormPos = geo_vector(input_cursor_x(ctx->input), input_cursor_y(ctx->input));
  const f32       inputAspect  = input_cursor_aspect(ctx->input);
  const GeoRay    inputRay     = scene_camera_ray(camera, cameraTrans, inputAspect, inputNormPos);
  const GeoPlane  groundPlane  = {.normal = geo_up};

  const f32 rayT = geo_plane_intersect_ray(&groundPlane, &inputRay);
  if (rayT < g_createMinInteractDist || rayT > g_createMaxInteractDist || blocked) {
    return;
  }
  const GeoVector pos = geo_ray_position(&inputRay, rayT);
  debug_sphere(ctx->shape, pos, 0.25f, geo_color_green, DebugShape_Overlay);

  debug_stats_notify(
      ctx->globalStats,
      string_lit("Prefab location"),
      fmt_write_scratch(
          "x: {<5} z: {<5}",
          fmt_float(pos.x, .minDecDigits = 1, .maxDecDigits = 1, .expThresholdNeg = 0),
          fmt_float(pos.z, .minDecDigits = 1, .maxDecDigits = 1, .expThresholdNeg = 0)));

  if (input_triggered_lit(ctx->input, "DebugPrefabCreate")) {
    prefab_create_accept(ctx, pos);
  }
}

static void prefab_options_normal_draw(UiCanvasComp* canvas, const PrefabPanelContext* ctx) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(5, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 60);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Filter:"));
  ui_table_next_column(canvas, &table);
  ui_textbox(
      canvas,
      &ctx->panelComp->idFilter,
      .placeholder = string_lit("*"),
      .tooltip     = g_tooltipFilter);

  ui_layout_pop(canvas);
}

static void prefab_options_create_draw(UiCanvasComp* canvas, const PrefabPanelContext* ctx) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(5, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 80);
  ui_table_add_column(&table, UiTableColumn_Fixed, 35);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_layout_move_dir(canvas, Ui_Right, 5, UiBase_Absolute);
  ui_label(canvas, string_lit("Create"));
  ui_table_next_column(canvas, &table);

  ui_label(canvas, string_lit("Multiple:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &ctx->panelComp->createMultiple);
  ui_table_next_column(canvas, &table);

  ui_label(canvas, string_lit("Faction:"));
  ui_table_next_column(canvas, &table);
  prefab_faction_select(canvas, &ctx->panelComp->createFaction);
  ui_table_next_column(canvas, &table);

  ui_layout_move_to(canvas, UiBase_Current, UiAlign_MiddleRight, Ui_X);
  ui_layout_resize(canvas, UiAlign_MiddleRight, ui_vector(75, 0), UiBase_Absolute, Ui_X);
  if (ui_button(canvas, .label = string_lit("Cancel"), .frameColor = ui_color(255, 16, 0, 192))) {
    prefab_create_cancel(ctx);
  }
  ui_layout_pop(canvas);
}

static void prefab_options_draw(UiCanvasComp* canvas, const PrefabPanelContext* ctx) {
  switch (ctx->panelComp->mode) {
  case PrefabPanelMode_Normal:
    prefab_options_normal_draw(canvas, ctx);
    break;
  case PrefabPanelMode_Create:
    prefab_options_create_draw(canvas, ctx);
    break;
  case PrefabPanelMode_Count:
    break;
  }
  ui_canvas_id_block_next(canvas);
}

static void prefab_panel_draw(UiCanvasComp* canvas, const PrefabPanelContext* ctx) {

  const String title = fmt_write_scratch("{} Prefab Panel", fmt_ui_shape(Construction));
  ui_panel_begin(canvas, &ctx->panelComp->panel, .title = title);

  prefab_options_draw(canvas, ctx);
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

  const f32 totalHeight = ui_table_height(&table, ctx->panelComp->totalRows);
  ui_scrollview_begin(canvas, &ctx->panelComp->scrollview, totalHeight);
  ctx->panelComp->totalRows = 0;

  for (u32 prefabIdx = 0; prefabIdx != ctx->prefabMap->prefabCount; ++prefabIdx) {
    AssetPrefab* prefab = &ctx->prefabMap->prefabs[prefabIdx];
    const String name   = stringtable_lookup(g_stringtable, prefab->nameHash);

    if (!prefab_filter(ctx, name)) {
      continue;
    }
    ++ctx->panelComp->totalRows;

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
        .panelComp   = panelComp,
        .input       = ecs_view_read_t(globalItr, InputManagerComp),
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
  ecs_register_comp(DebugPrefabPanelComp, .destructor = ecs_destruct_prefab_panel);

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
      .idFilter      = dynstring_create(g_alloc_heap, 32),
      .scrollview    = ui_scrollview(),
      .panel         = ui_panel(.position = ui_vector(0.2f, 0.3f), .size = ui_vector(500, 350)));
  return panelEntity;
}
