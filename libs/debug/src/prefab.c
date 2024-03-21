#include "asset_prefab.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_stringtable.h"
#include "debug_panel.h"
#include "debug_prefab.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "debug_stats.h"
#include "ecs_world.h"
#include "input_manager.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_prefab.h"
#include "scene_set.h"
#include "scene_terrain.h"
#include "ui.h"

#include "widget_internal.h"

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

typedef enum {
  PrefabCreateFlags_Multiple        = 1 << 0,
  PrefabCreateFlags_AutoSelect      = 1 << 1,
  PrefabCreateFlags_RandomRotationY = 1 << 2,

  PrefabCreateFlags_Default = PrefabCreateFlags_AutoSelect
} PrefabCreateFlags;

ecs_comp_define(DebugPrefabPanelComp) {
  PrefabPanelMode   mode;
  PrefabCreateFlags createFlags;
  StringHash        createPrefabId;
  SceneFaction      createFaction;
  f32               createScale;
  DynString         idFilter;
  UiPanel           panel;
  UiScrollview      scrollview;
  u32               totalRows;
};

static void ecs_destruct_prefab_panel(void* data) {
  DebugPrefabPanelComp* comp = data;
  dynstring_destroy(&comp->idFilter);
}

typedef struct {
  EcsWorld*                    world;
  const AssetPrefabMapComp*    prefabMap;
  const SceneCollisionEnvComp* collision;
  const SceneTerrainComp*      terrain;
  DebugPrefabPanelComp*        panelComp;
  const InputManagerComp*      input;
  DebugShapeComp*              shape;
  DebugStatsGlobalComp*        globalStats;
  SceneSetEnvComp*             setEnv;
} PrefabPanelContext;

ecs_view_define(PrefabMapView) { ecs_access_read(AssetPrefabMapComp); }
ecs_view_define(PrefabInstanceView) { ecs_access_read(ScenePrefabInstanceComp); }
ecs_view_define(CameraView) {
  ecs_access_read(SceneCameraComp);
  ecs_access_read(SceneTransformComp);
}

static bool prefab_filter(const PrefabPanelContext* ctx, const String prefabName) {
  if (!ctx->panelComp->idFilter.size) {
    return true;
  }
  const String rawFilter = dynstring_view(&ctx->panelComp->idFilter);
  const String filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));
  return string_match_glob(prefabName, filter, StringMatchFlags_IgnoreCase);
}

static void prefab_instance_counts(const PrefabPanelContext* ctx, u32 out[], const u32 maxCount) {
  mem_set(mem_from_to(out, out + math_min(maxCount, ctx->prefabMap->prefabCount)), 0);

  EcsView* prefabInstanceView = ecs_world_view_t(ctx->world, PrefabInstanceView);
  for (EcsIterator* itr = ecs_view_itr(prefabInstanceView); ecs_view_walk(itr);) {
    const ScenePrefabInstanceComp* instComp = ecs_view_read_t(itr, ScenePrefabInstanceComp);

    const u16 prefabIndex = asset_prefab_get_index(ctx->prefabMap, instComp->prefabId);
    // NOTE: PrefabIndex can be sentinel_u16 if the prefabMap was hot-loaded after spawning.
    if (prefabIndex < maxCount) {
      ++out[prefabIndex];
    }
  }
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

  if (!(input_modifiers(ctx->input) & InputModifier_Control)) {
    scene_set_clear(ctx->setEnv, g_sceneSetSelected);
  }

  EcsView* prefabInstanceView = ecs_world_view_t(ctx->world, PrefabInstanceView);
  for (EcsIterator* itr = ecs_view_itr(prefabInstanceView); ecs_view_walk(itr);) {
    const ScenePrefabInstanceComp* instComp = ecs_view_read_t(itr, ScenePrefabInstanceComp);

    if (instComp->prefabId == prefabId) {
      scene_set_add(ctx->setEnv, g_sceneSetSelected, ecs_view_entity(itr));
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

  GeoQuat rot = geo_quat_ident;
  if (ctx->panelComp->createFlags & PrefabCreateFlags_RandomRotationY) {
    rot = geo_quat_angle_axis(rng_sample_f32(g_rng) * math_pi_f32 * 2.0f, geo_up);
  }

  const EcsEntityId spawnedEntity = scene_prefab_spawn(
      ctx->world,
      &(ScenePrefabSpec){
          .prefabId = ctx->panelComp->createPrefabId,
          .position = pos,
          .rotation = rot,
          .scale    = ctx->panelComp->createScale,
          .faction  = ctx->panelComp->createFaction,
      });

  scene_set_clear(ctx->setEnv, g_sceneSetSelected);

  bool select = (ctx->panelComp->createFlags & PrefabCreateFlags_AutoSelect) != 0;
  if (input_modifiers(ctx->input) & InputModifier_Shift) {
    select = false;
  }
  if (select) {
    scene_set_add(ctx->setEnv, g_sceneSetSelected, spawnedEntity);
  }

  if (!(ctx->panelComp->createFlags & PrefabCreateFlags_Multiple)) {
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
  if (!input_layer_active(ctx->input, string_hash_lit("Debug"))) {
    prefab_create_cancel(ctx); // Debug input no longer active.
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

  f32 rayT = -1.0f;
  if (ctx->collision) {
    const SceneQueryFilter filter = {.layerMask = SceneLayer_Environment};
    SceneRayHit            hit;
    if (scene_query_ray(ctx->collision, &inputRay, g_createMaxInteractDist, &filter, &hit)) {
      rayT = hit.time;
    }
  }
  if (rayT < 0 && scene_terrain_loaded(ctx->terrain)) {
    rayT = scene_terrain_intersect_ray(ctx->terrain, &inputRay, g_createMaxInteractDist);
  }
  if (rayT < 0) {
    rayT = geo_plane_intersect_ray(&(GeoPlane){.normal = geo_up}, &inputRay);
  }
  if (rayT < g_createMinInteractDist || blocked) {
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

static void prefab_panel_normal_options_draw(UiCanvasComp* canvas, const PrefabPanelContext* ctx) {
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

static void prefab_panel_normal_draw(UiCanvasComp* canvas, const PrefabPanelContext* ctx) {
  prefab_panel_normal_options_draw(canvas, ctx);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  /**
   * NOTE: Disable creating when debug input is not active, reason is placing prefabs uses debug
   * input to detect place accept / cancel. This can happen when pinning the window.
   */
  const bool disableCreate = !input_layer_active(ctx->input, string_hash_lit("Debug"));

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 225);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Name"), string_lit("Prefab name.")},
          {string_lit("Count"), string_lit("Amount of currently spawned instances.")},
          {string_lit("Actions"), string_empty},
      });

  u32 instanceCounts[1024];
  prefab_instance_counts(ctx, instanceCounts, array_elems(instanceCounts));

  const f32 totalHeight = ui_table_height(&table, ctx->panelComp->totalRows);
  ui_scrollview_begin(canvas, &ctx->panelComp->scrollview, totalHeight);
  ctx->panelComp->totalRows = 0;

  for (u16 userIndex = 0; userIndex != ctx->prefabMap->prefabCount; ++userIndex) {
    const u16    prefabIdx = asset_prefab_get_index_from_user(ctx->prefabMap, userIndex);
    AssetPrefab* prefab    = &ctx->prefabMap->prefabs[prefabIdx];
    const String name      = stringtable_lookup(g_stringtable, prefab->nameHash);

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

    const u32 count = prefabIdx < array_elems(instanceCounts) ? instanceCounts[prefabIdx] : 0;
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(count)));
    ui_table_next_column(canvas, &table);

    ui_layout_resize(canvas, UiAlign_MiddleLeft, ui_vector(25, 0), UiBase_Absolute, Ui_X);
    if (ui_button(
            canvas,
            .label      = ui_shape_scratch(UiShape_Delete),
            .fontSize   = 18,
            .frameColor = ui_color(255, 16, 0, 192),
            .tooltip    = string_lit("Destroy all instances."))) {
      prefab_destroy_all(ctx, prefab->nameHash);
    }
    ui_layout_next(canvas, Ui_Right, 10);
    if (ui_button(
            canvas,
            .label      = ui_shape_scratch(UiShape_SelectAll),
            .fontSize   = 18,
            .frameColor = ui_color(0, 16, 255, 192),
            .tooltip    = string_lit("Select all instances."))) {
      prefab_select_all(ctx, prefab->nameHash);
    }
    ui_layout_next(canvas, Ui_Right, 10);
    if (ui_button(
            canvas,
            .flags      = disableCreate ? UiWidget_Disabled : 0,
            .label      = ui_shape_scratch(UiShape_Add),
            .fontSize   = 18,
            .frameColor = disableCreate ? ui_color(64, 64, 64, 192) : ui_color(16, 192, 0, 192),
            .tooltip    = string_lit("Create a new instance."))) {
      prefab_create_start(ctx, prefab->nameHash);
    }
  }

  ui_scrollview_end(canvas, &ctx->panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void prefab_panel_create_draw(UiCanvasComp* canvas, const PrefabPanelContext* ctx) {
  ui_layout_push(canvas);

  const AssetPrefab* prefab = asset_prefab_get(ctx->prefabMap, ctx->panelComp->createPrefabId);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Create"));
  ui_table_next_column(canvas, &table);
  if (ui_button(canvas, .label = string_lit("Cancel"), .frameColor = ui_color(255, 16, 0, 192))) {
    prefab_create_cancel(ctx);
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Multiple"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, &ctx->panelComp->createFlags, PrefabCreateFlags_Multiple);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Auto Select"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, &ctx->panelComp->createFlags, PrefabCreateFlags_AutoSelect);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Faction"));
  ui_table_next_column(canvas, &table);
  debug_widget_editor_faction(canvas, &ctx->panelComp->createFaction);

  if (asset_prefab_trait_get(ctx->prefabMap, prefab, AssetPrefabTrait_Scalable)) {
    ui_table_next_row(canvas, &table);
    ui_label(canvas, string_lit("Scale"));
    ui_table_next_column(canvas, &table);
    ui_slider(canvas, &ctx->panelComp->createScale, .min = 0.1f, .max = 5.0f);
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Random Rotation Y"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, &ctx->panelComp->createFlags, PrefabCreateFlags_RandomRotationY);

  ui_layout_pop(canvas);
}

static void prefab_panel_draw(UiCanvasComp* canvas, const PrefabPanelContext* ctx) {
  const String title = fmt_write_scratch("{} Prefab Panel", fmt_ui_shape(Construction));
  ui_panel_begin(
      canvas, &ctx->panelComp->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  switch (ctx->panelComp->mode) {
  case PrefabPanelMode_Normal:
    prefab_panel_normal_draw(canvas, ctx);
    break;
  case PrefabPanelMode_Create:
    prefab_panel_create_draw(canvas, ctx);
    break;
  case PrefabPanelMode_Count:
    UNREACHABLE
  }

  ui_panel_end(canvas, &ctx->panelComp->panel);
}

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_maybe_read(SceneCollisionEnvComp);
  ecs_access_read(ScenePrefabEnvComp);
  ecs_access_read(SceneTerrainComp);
  ecs_access_write(DebugShapeComp);
  ecs_access_write(DebugStatsGlobalComp);
  ecs_access_write(InputManagerComp);
  ecs_access_write(SceneSetEnvComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_access_read(DebugPanelComp);
  ecs_access_write(DebugPrefabPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DebugPrefabUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const ScenePrefabEnvComp*    prefabEnv = ecs_view_read_t(globalItr, ScenePrefabEnvComp);
  const SceneCollisionEnvComp* collision = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneTerrainComp*      terrain   = ecs_view_read_t(globalItr, SceneTerrainComp);
  InputManagerComp*            input     = ecs_view_write_t(globalItr, InputManagerComp);

  EcsView*     mapView = ecs_world_view_t(world, PrefabMapView);
  EcsIterator* mapItr  = ecs_view_maybe_at(mapView, scene_prefab_map(prefabEnv));
  if (!mapItr) {
    return;
  }
  const AssetPrefabMapComp* prefabMap = ecs_view_read_t(mapItr, AssetPrefabMapComp);

  bool creatingPrefab = false;

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugPrefabPanelComp* panelComp = ecs_view_write_t(itr, DebugPrefabPanelComp);
    UiCanvasComp*         canvas    = ecs_view_write_t(itr, UiCanvasComp);

    const PrefabPanelContext ctx = {
        .world       = world,
        .prefabMap   = prefabMap,
        .collision   = collision,
        .terrain     = terrain,
        .panelComp   = panelComp,
        .input       = input,
        .shape       = ecs_view_write_t(globalItr, DebugShapeComp),
        .globalStats = ecs_view_write_t(globalItr, DebugStatsGlobalComp),
        .setEnv      = ecs_view_write_t(globalItr, SceneSetEnvComp),
    };

    ui_canvas_reset(canvas);

    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (debug_panel_hidden(ecs_view_read_t(itr, DebugPanelComp)) && !pinned) {
      if (panelComp->mode == PrefabPanelMode_Create) {
        prefab_create_cancel(&ctx);
      }
      continue;
    }

    switch (panelComp->mode) {
    case PrefabPanelMode_Create:
      prefab_create_update(&ctx);
      creatingPrefab |= true;
      break;
    case PrefabPanelMode_Normal:
    case PrefabPanelMode_Count:
      break;
    }
    prefab_panel_draw(canvas, &ctx);

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }

  input_blocker_update(input, InputBlocker_PrefabCreate, creatingPrefab);
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
  const EcsEntityId panelEntity = debug_panel_create(world, window);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugPrefabPanelComp,
      .mode          = PrefabPanelMode_Normal,
      .createFlags   = PrefabCreateFlags_Default,
      .createFaction = SceneFaction_A,
      .createScale   = 1.0f,
      .idFilter      = dynstring_create(g_alloc_heap, 32),
      .scrollview    = ui_scrollview(),
      .panel         = ui_panel(.position = ui_vector(1.0f, 0.0f), .size = ui_vector(500, 350)));
  return panelEntity;
}
