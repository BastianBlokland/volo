#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "debug_level.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "input_manager.h"
#include "scene_camera.h"
#include "scene_level.h"
#include "scene_transform.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_scrollview.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_table.h"
#include "ui_widget.h"

#include "widget_internal.h"

// clang-format off

static const String g_tooltipReload       = string_static("Reload the current level.");
static const String g_tooltipUnload       = string_static("Unload the current level.");
static const String g_tooltipSave         = string_static("Save the current level.");
static const String g_tooltipFilter       = string_static("Filter levels by identifier.\nSupports glob characters \a.b*\ar and \a.b?\ar (\a.b!\ar prefix to invert).");
static const String g_queryPatternLevel   = string_static("levels/*.level");
static const String g_queryPatternTerrain = string_static("terrains/*.terrain");

// clang-format on

typedef enum {
  DebugLevelFlags_RefreshAssets = 1 << 0,
  DebugLevelFlags_Reload        = 1 << 1,
  DebugLevelFlags_Unload        = 1 << 2,
  DebugLevelFlags_Save          = 1 << 3,

  DebugLevelFlags_None    = 0,
  DebugLevelFlags_Default = DebugLevelFlags_RefreshAssets,
} DebugLevelFlags;

typedef enum {
  DebugLevelTab_Manage,
  DebugLevelTab_Settings,

  DebugLevelTab_Count,
} DebugLevelTab;

static const String g_levelTabNames[] = {
    string_static("Manage"),
    string_static("\uE8B8 Settings"),
};
ASSERT(array_elems(g_levelTabNames) == DebugLevelTab_Count, "Incorrect number of names");

static const String g_levelFogNames[] = {
    string_static("Disabled"),
    string_static("VisibilityBased"),
};
ASSERT(array_elems(g_levelFogNames) == AssetLevelFog_Count, "Incorrect number of names");

ecs_comp_define(DebugLevelPanelComp) {
  DebugLevelFlags flags;
  EcsEntityId     window;
  DynString       idFilter;
  DynString       nameBuffer;
  DynArray        assetsLevel;   // EcsEntityId[]
  DynArray        assetsTerrain; // EcsEntityId[]
  UiPanel         panel;
  UiScrollview    scrollview;
  u32             totalRows;
};

static void ecs_destruct_level_panel(void* data) {
  DebugLevelPanelComp* comp = data;
  dynstring_destroy(&comp->idFilter);
  dynstring_destroy(&comp->nameBuffer);
  dynarray_destroy(&comp->assetsLevel);
  dynarray_destroy(&comp->assetsTerrain);
}

ecs_view_define(AssetView) { ecs_access_read(AssetComp); }

ecs_view_define(CameraView) {
  ecs_access_with(SceneCameraComp);
  ecs_access_read(SceneTransformComp);
}

typedef struct {
  EcsWorld*                 world;
  DebugLevelPanelComp*      panelComp;
  SceneLevelManagerComp*    levelManager;
  AssetManagerComp*         assets;
  EcsView*                  assetView;
  const SceneTransformComp* cameraTrans;
} DebugLevelContext;

static GeoVector level_camera_center(const DebugLevelContext* ctx) {
  static const GeoPlane g_groundPlane = {.normal = {.y = 1.0f}};
  if (ctx->cameraTrans) {
    const GeoRay cameraRay = {
        .point = ctx->cameraTrans->position,
        .dir   = geo_quat_rotate(ctx->cameraTrans->rotation, geo_forward),
    };
    const f32 rayT = geo_plane_intersect_ray(&g_groundPlane, &cameraRay);
    if (rayT > f32_epsilon) {
      return geo_ray_position(&cameraRay, rayT);
    }
  }
  return geo_vector(0);
}

static void level_assets_refresh(DebugLevelContext* ctx, const String pattern, DynString* out) {
  EcsEntityId assetEntities[asset_query_max_results];
  const u32   assetCount = asset_query(ctx->world, ctx->assets, pattern, assetEntities);

  dynarray_clear(out);
  for (u32 i = 0; i != assetCount; ++i) {
    *dynarray_push_t(out, EcsEntityId) = assetEntities[i];
  }
}

static bool level_asset_select(
    UiCanvasComp* c, DebugLevelContext* ctx, EcsEntityId* val, const DynArray* options) {
  EcsIterator* assetItr     = ecs_view_itr(ctx->assetView);
  String       names[32]    = {[0] = string_lit("< None >")};
  EcsEntityId  entities[32] = {[0] = 0};
  u32          count        = 1;
  i32          index        = 0;
  for (usize i = 0; i != options->size; ++i) {
    if (count == array_elems(names)) {
      break; // Max option count exceeded; Should we log a warning?
    }
    const EcsEntityId asset = *dynarray_at_t(options, i, EcsEntityId);
    if (ecs_view_maybe_jump(assetItr, asset)) {
      if (asset == *val) {
        index = count;
      }
      entities[count] = asset;
      names[count++]  = asset_id(ecs_view_read_t(assetItr, AssetComp));
    }
  }
  if (ui_select(c, &index, names, count)) {
    *val = entities[index];
    return true;
  }
  return false;
}

static bool level_id_filter(DebugLevelContext* ctx, const String levelId) {
  if (!ctx->panelComp->idFilter.size) {
    return true;
  }
  const String rawFilter = dynstring_view(&ctx->panelComp->idFilter);
  const String filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));
  return string_match_glob(levelId, filter, StringMatchFlags_IgnoreCase);
}

static void manage_panel_options_draw(UiCanvasComp* c, DebugLevelContext* ctx) {
  ui_layout_push(c);

  UiTable table = ui_table(.spacing = ui_vector(5, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 30);
  ui_table_add_column(&table, UiTableColumn_Fixed, 30);
  ui_table_add_column(&table, UiTableColumn_Fixed, 30);
  ui_table_add_column(&table, UiTableColumn_Fixed, 60);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(c, &table);

  const bool          isLoaded = ecs_entity_valid(scene_level_asset(ctx->levelManager));
  const UiWidgetFlags btnFlags = isLoaded ? 0 : UiWidget_Disabled;

  if (ui_button(c, .flags = btnFlags, .label = string_lit("\uE5D5"), .tooltip = g_tooltipReload)) {
    ctx->panelComp->flags |= DebugLevelFlags_Reload;
  }
  ui_table_next_column(c, &table);
  if (ui_button(c, .flags = btnFlags, .label = string_lit("\uE161"), .tooltip = g_tooltipSave)) {
    ctx->panelComp->flags |= DebugLevelFlags_Save;
  }
  ui_table_next_column(c, &table);
  if (ui_button(c, .flags = btnFlags, .label = string_lit("\uE9BA"), .tooltip = g_tooltipUnload)) {
    ctx->panelComp->flags |= DebugLevelFlags_Unload;
  }
  ui_table_next_column(c, &table);
  ui_label(c, string_lit("Filter:"));
  ui_table_next_column(c, &table);
  ui_textbox(
      c, &ctx->panelComp->idFilter, .placeholder = string_lit("*"), .tooltip = g_tooltipFilter);

  ui_layout_pop(c);
}

static void manage_panel_draw(UiCanvasComp* c, DebugLevelContext* ctx) {
  manage_panel_options_draw(c, ctx);
  ui_layout_grow(c, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(c, UiClip_None);

  const bool isLoading = scene_level_loading(ctx->levelManager);
  const bool disabled  = isLoading;
  ui_style_push(c);
  if (disabled) {
    ui_style_color_mult(c, 0.5f);
  }

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 350);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      c,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Level"), string_lit("Level identifier.")},
          {string_lit("Actions"), string_empty},
      });

  const f32 totalHeight = ui_table_height(&table, ctx->panelComp->totalRows);
  ui_scrollview_begin(c, &ctx->panelComp->scrollview, totalHeight);
  ctx->panelComp->totalRows = 0;

  EcsIterator* assetItr = ecs_view_itr(ctx->assetView);
  dynarray_for_t(&ctx->panelComp->assetsLevel, EcsEntityId, levelAsset) {
    if (!ecs_view_maybe_jump(assetItr, *levelAsset)) {
      continue;
    }
    const String id     = asset_id(ecs_view_read_t(assetItr, AssetComp));
    const bool   loaded = scene_level_asset(ctx->levelManager) == *levelAsset;

    if (!level_id_filter(ctx, id)) {
      continue;
    }
    ++ctx->panelComp->totalRows;

    ui_table_next_row(c, &table);
    ui_table_draw_row_bg(c, &table, loaded ? ui_color(16, 64, 16, 192) : ui_color(48, 48, 48, 192));

    ui_label(c, id, .selectable = true);
    ui_table_next_column(c, &table);

    ui_layout_resize(c, UiAlign_MiddleLeft, ui_vector(60, 0), UiBase_Absolute, Ui_X);
    if (ui_button(c, .flags = disabled ? UiWidget_Disabled : 0, .label = string_lit("Load"))) {
      scene_level_load(ctx->world, *levelAsset);
    }
  }

  ui_scrollview_end(c, &ctx->panelComp->scrollview);

  ui_style_pop(c);
  ui_layout_container_pop(c);
}

static void settings_panel_draw(UiCanvasComp* c, DebugLevelContext* ctx) {
  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(c, &table);
  ui_label(c, string_lit("Name"));
  ui_table_next_column(c, &table);

  dynstring_clear(&ctx->panelComp->nameBuffer);
  dynstring_append(&ctx->panelComp->nameBuffer, scene_level_name(ctx->levelManager));

  if (ui_textbox(c, &ctx->panelComp->nameBuffer, .maxTextLength = 32)) {
    scene_level_name_update(ctx->levelManager, dynstring_view(&ctx->panelComp->nameBuffer));
  }

  ui_table_next_row(c, &table);
  ui_label(c, string_lit("Terrain"));
  ui_table_next_column(c, &table);

  EcsEntityId terrain = scene_level_terrain(ctx->levelManager);
  if (level_asset_select(c, ctx, &terrain, &ctx->panelComp->assetsTerrain)) {
    scene_level_terrain_update(ctx->levelManager, terrain);
  }

  ui_table_next_row(c, &table);
  ui_label(c, string_lit("Fog"));
  ui_table_next_column(c, &table);
  AssetLevelFog fog = scene_level_fog(ctx->levelManager);
  if (ui_select(c, (i32*)&fog, g_levelFogNames, array_elems(g_levelFogNames))) {
    scene_level_fog_update(ctx->levelManager, fog);
  }

  ui_table_next_row(c, &table);
  ui_label(c, string_lit("Startpoint"));
  ui_table_next_column(c, &table);

  GeoVector startpoint = scene_level_startpoint(ctx->levelManager);
  if (debug_widget_editor_vec3_resettable(c, &startpoint, UiWidget_Default)) {
    scene_level_startpoint_update(ctx->levelManager, startpoint);
  }

  ui_table_next_row(c, &table);
  ui_table_next_column(c, &table);
  if (ui_button(c, .label = string_lit("Camera center"))) {
    const GeoVector newStartpoint = level_camera_center(ctx);
    scene_level_startpoint_update(ctx->levelManager, newStartpoint);
  }

  ui_layout_push(c);
  ui_layout_inner(c, UiBase_Container, UiAlign_BottomCenter, ui_vector(100, 22), UiBase_Absolute);
  ui_layout_move_dir(c, Ui_Up, 8, UiBase_Absolute);
  if (ui_button(c, .label = string_lit("Save"), .tooltip = g_tooltipSave)) {
    ctx->panelComp->flags |= DebugLevelFlags_Save;
  }
  ui_layout_pop(c);
}

static void level_panel_draw(UiCanvasComp* c, DebugLevelContext* ctx) {
  const String title = fmt_write_scratch("{} Level Panel", fmt_ui_shape(Globe));
  ui_panel_begin(
      c,
      &ctx->panelComp->panel,
      .title       = title,
      .tabNames    = g_levelTabNames,
      .tabCount    = DebugLevelTab_Count,
      .topBarColor = ui_color(100, 0, 0, 192));

  switch (ctx->panelComp->panel.activeTab) {
  case DebugLevelTab_Manage:
    manage_panel_draw(c, ctx);
    break;
  case DebugLevelTab_Settings:
    if (!ecs_entity_valid(scene_level_asset(ctx->levelManager))) {
      ui_label(c, string_lit("< No loaded level >"), .align = UiAlign_MiddleCenter);
      break;
    }
    settings_panel_draw(c, ctx);
    break;
  }

  ui_panel_end(c, &ctx->panelComp->panel);
}

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_write(AssetManagerComp);
  ecs_access_write(SceneLevelManagerComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DebugLevelPanelComp's are exclusively managed here.

  ecs_access_read(DebugPanelComp);
  ecs_access_write(DebugLevelPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DebugLevelUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneLevelManagerComp*  levelManager = ecs_view_write_t(globalItr, SceneLevelManagerComp);
  AssetManagerComp*       assets       = ecs_view_write_t(globalItr, AssetManagerComp);
  const InputManagerComp* input        = ecs_view_read_t(globalItr, InputManagerComp);

  EcsView* assetView  = ecs_world_view_t(world, AssetView);
  EcsView* cameraView = ecs_world_view_t(world, CameraView);
  EcsView* panelView  = ecs_world_view_t(world, PanelUpdateView);

  if (input_triggered_lit(input, "SaveLevel")) {
    const EcsEntityId currentLevelAsset = scene_level_asset(levelManager);
    if (currentLevelAsset) {
      scene_level_save(world, currentLevelAsset);
    }
  }

  EcsIterator* cameraItr = ecs_view_itr(cameraView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugLevelPanelComp* panelComp = ecs_view_write_t(itr, DebugLevelPanelComp);
    UiCanvasComp*        canvas    = ecs_view_write_t(itr, UiCanvasComp);

    DebugLevelContext ctx = {
        .world        = world,
        .panelComp    = panelComp,
        .levelManager = levelManager,
        .assets       = assets,
        .assetView    = assetView,
    };

    ecs_view_itr_reset(cameraItr);

    // NOTE: Detached panels have no camera on the window; in that case use the first found camera.
    if (ecs_view_maybe_jump(cameraItr, panelComp->window) || ecs_view_walk(cameraItr)) {
      ctx.cameraTrans = ecs_view_read_t(cameraItr, SceneTransformComp);
    }

    if (panelComp->flags & DebugLevelFlags_RefreshAssets) {
      level_assets_refresh(&ctx, g_queryPatternLevel, &panelComp->assetsLevel);
      level_assets_refresh(&ctx, g_queryPatternTerrain, &panelComp->assetsTerrain);
      panelComp->flags &= ~DebugLevelFlags_RefreshAssets;
    }
    if (panelComp->flags & DebugLevelFlags_Reload) {
      scene_level_reload(world);
      panelComp->flags &= ~DebugLevelFlags_Reload;
    }
    if (panelComp->flags & DebugLevelFlags_Unload) {
      scene_level_unload(world);
      panelComp->flags &= ~DebugLevelFlags_Unload;
    }
    if (panelComp->flags & DebugLevelFlags_Save) {
      scene_level_save(world, scene_level_asset(levelManager));
      panelComp->flags &= ~DebugLevelFlags_Save;
    }

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (debug_panel_hidden(ecs_view_read_t(itr, DebugPanelComp)) && !pinned) {
      continue;
    }
    level_panel_draw(canvas, &ctx);

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_level_module) {
  ecs_register_comp(DebugLevelPanelComp, .destructor = ecs_destruct_level_panel);

  ecs_register_view(AssetView);
  ecs_register_view(CameraView);
  ecs_register_view(PanelUpdateGlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugLevelUpdatePanelSys,
      ecs_view_id(AssetView),
      ecs_view_id(CameraView),
      ecs_view_id(PanelUpdateGlobalView),
      ecs_view_id(PanelUpdateView));
}

EcsEntityId
debug_level_panel_open(EcsWorld* world, const EcsEntityId window, const DebugPanelType type) {
  const EcsEntityId    panelEntity = debug_panel_create(world, window, type);
  DebugLevelPanelComp* levelPanel  = ecs_world_add_t(
      world,
      panelEntity,
      DebugLevelPanelComp,
      .flags         = DebugLevelFlags_Default,
      .window        = window,
      .idFilter      = dynstring_create(g_allocHeap, 32),
      .nameBuffer    = dynstring_create(g_allocHeap, 32),
      .assetsLevel   = dynarray_create_t(g_allocHeap, EcsEntityId, 8),
      .assetsTerrain = dynarray_create_t(g_allocHeap, EcsEntityId, 8),
      .panel         = ui_panel(.position = ui_vector(0.5f, 0.5f), .size = ui_vector(500, 300)));

  if (type == DebugPanelType_Detached) {
    ui_panel_maximize(&levelPanel->panel);
  }

  return panelEntity;
}
