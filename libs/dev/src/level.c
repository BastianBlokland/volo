#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_float.h"
#include "dev_finder.h"
#include "dev_level.h"
#include "dev_panel.h"
#include "dev_widget.h"
#include "ecs_entity.h"
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

// clang-format off

static const String g_tooltipEdit   = string_static("Start editing the current level.");
static const String g_tooltipPlay   = string_static("Start playing the current level.");
static const String g_tooltipUnload = string_static("Unload the current level.");
static const String g_tooltipSave   = string_static("Save the current level.");
static const String g_tooltipFilter = string_static("Filter levels by identifier.\nSupports glob characters \a.b*\ar and \a.b?\ar (\a.b!\ar prefix to invert).");

// clang-format on

typedef enum {
  DevLevelFlags_RefreshLevels = 1 << 0,
  DevLevelFlags_Edit          = 1 << 1,
  DevLevelFlags_Play          = 1 << 2,
  DevLevelFlags_Unload        = 1 << 3,
  DevLevelFlags_Save          = 1 << 4,

  DevLevelFlags_None     = 0,
  DevLevelFlags_Default  = DevLevelFlags_RefreshLevels,
  DevLevelFlags_Volatile = DevLevelFlags_RefreshLevels | DevLevelFlags_Edit | DevLevelFlags_Play |
                           DevLevelFlags_Unload | DevLevelFlags_Save,
} DevLevelFlags;

typedef enum {
  DevLevelTab_Manage,
  DevLevelTab_Settings,

  DevLevelTab_Count,
} DevLevelTab;

static const String g_levelTabNames[] = {
    string_static("Manage"),
    string_static("\uE8B8 Settings"),
};
ASSERT(array_elems(g_levelTabNames) == DevLevelTab_Count, "Incorrect number of names");

static const String g_levelFogNames[] = {
    string_static("Disabled"),
    string_static("VisibilityBased"),
};
ASSERT(array_elems(g_levelFogNames) == AssetLevelFog_Count, "Incorrect number of names");

ecs_comp_define(DevLevelPanelComp) {
  DevLevelFlags flags;
  EcsEntityId   window;
  DynString     idFilter;
  DynString     nameBuffer;
  UiPanel       panel;
  UiScrollview  scrollview;
  u32           totalRows;
};

static void ecs_destruct_level_panel(void* data) {
  DevLevelPanelComp* comp = data;
  dynstring_destroy(&comp->idFilter);
  dynstring_destroy(&comp->nameBuffer);
}

ecs_view_define(CameraView) {
  ecs_access_with(SceneCameraComp);
  ecs_access_read(SceneTransformComp);
}

typedef struct {
  EcsWorld*                 world;
  DevLevelPanelComp*        panelComp;
  SceneLevelManagerComp*    levelManager;
  DevFinderComp*            finder;
  const SceneTransformComp* cameraTrans;
} DevLevelContext;

static GeoVector level_camera_center(const DevLevelContext* ctx) {
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

static bool level_id_filter(DevLevelContext* ctx, const String levelId) {
  if (!ctx->panelComp->idFilter.size) {
    return true;
  }
  const String rawFilter = dynstring_view(&ctx->panelComp->idFilter);
  const String filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));
  return string_match_glob(levelId, filter, StringMatchFlags_IgnoreCase);
}

static void manage_panel_options_draw(UiCanvasComp* c, DevLevelContext* ctx) {
  ui_layout_push(c);

  UiTable table = ui_table(.spacing = ui_vector(5, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 30);
  ui_table_add_column(&table, UiTableColumn_Fixed, 30);
  ui_table_add_column(&table, UiTableColumn_Fixed, 30);
  ui_table_add_column(&table, UiTableColumn_Fixed, 30);
  ui_table_add_column(&table, UiTableColumn_Fixed, 60);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(c, &table);

  const bool isLoaded   = ecs_entity_valid(scene_level_asset(ctx->levelManager));
  const bool isEditMode = isLoaded && scene_level_mode(ctx->levelManager) == SceneLevelMode_Edit;

  const UiWidgetFlags btnFlags  = isLoaded ? 0 : UiWidget_Disabled;
  const UiWidgetFlags editFlags = isEditMode ? 0 : UiWidget_Disabled;

  if (ui_button(c, .flags = btnFlags, .label = string_lit("\uE3C9"), .tooltip = g_tooltipEdit)) {
    ctx->panelComp->flags |= DevLevelFlags_Edit;
  }
  ui_table_next_column(c, &table);
  if (ui_button(c, .flags = btnFlags, .label = string_lit("\uE037"), .tooltip = g_tooltipPlay)) {
    ctx->panelComp->flags |= DevLevelFlags_Play;
  }
  ui_table_next_column(c, &table);
  if (ui_button(c, .flags = editFlags, .label = string_lit("\uE161"), .tooltip = g_tooltipSave)) {
    ctx->panelComp->flags |= DevLevelFlags_Save;
  }
  ui_table_next_column(c, &table);
  if (ui_button(c, .flags = btnFlags, .label = string_lit("\uE9BA"), .tooltip = g_tooltipUnload)) {
    ctx->panelComp->flags |= DevLevelFlags_Unload;
  }
  ui_table_next_column(c, &table);
  ui_label(c, string_lit("Filter:"));
  ui_table_next_column(c, &table);
  ui_textbox(
      c, &ctx->panelComp->idFilter, .placeholder = string_lit("*"), .tooltip = g_tooltipFilter);

  ui_layout_pop(c);
}

static void manage_panel_draw(UiCanvasComp* c, DevLevelContext* ctx) {
  manage_panel_options_draw(c, ctx);
  ui_layout_grow(c, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(c, UiClip_None, UiLayer_Normal);

  const bool isLoading = scene_level_loading(ctx->levelManager);
  const bool disabled  = isLoading;
  ui_style_push(c);
  if (disabled) {
    ui_style_color_mult(c, 0.5f);
  }

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 375);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      c,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Level"), string_lit("Level identifier.")},
          {string_lit("Actions"), string_empty},
      });

  const f32 totalHeight = ui_table_height(&table, ctx->panelComp->totalRows);
  ui_scrollview_begin(c, &ctx->panelComp->scrollview, UiLayer_Normal, totalHeight);
  ctx->panelComp->totalRows = 0;

  const DevFinderResult levels = dev_finder_get(ctx->finder, DevFinder_Level);
  for (u32 i = 0; i != levels.count; ++i) {
    const EcsEntityId asset  = levels.entities[i];
    const String      id     = levels.ids[i];
    const bool        loaded = scene_level_asset(ctx->levelManager) == asset;
    if (!level_id_filter(ctx, id)) {
      continue;
    }
    ++ctx->panelComp->totalRows;

    ui_table_next_row(c, &table);
    ui_table_draw_row_bg(c, &table, loaded ? ui_color(16, 64, 16, 192) : ui_color(48, 48, 48, 192));

    ui_label(c, id, .selectable = true);
    ui_table_next_column(c, &table);

    ui_layout_resize(c, UiAlign_MiddleLeft, ui_vector(30, 0), UiBase_Absolute, Ui_X);
    if (ui_button(c, .flags = disabled ? UiWidget_Disabled : 0, .label = string_lit("\uE3C9"))) {
      scene_level_load(ctx->world, SceneLevelMode_Edit, asset);
    }
    ui_layout_next(c, Ui_Right, 10);
    if (ui_button(c, .flags = disabled ? UiWidget_Disabled : 0, .label = string_lit("\uE037"))) {
      scene_level_load(ctx->world, SceneLevelMode_Play, asset);
    }
  }

  ui_scrollview_end(c, &ctx->panelComp->scrollview);

  ui_style_pop(c);
  ui_layout_container_pop(c);
}

static void settings_panel_draw(UiCanvasComp* c, DevLevelContext* ctx) {
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
  if (dev_widget_asset(c, ctx->finder, DevFinder_Terrain, &terrain, UiWidget_Default)) {
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
  if (dev_widget_vec3_resettable(c, &startpoint, UiWidget_Default)) {
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
    ctx->panelComp->flags |= DevLevelFlags_Save;
  }
  ui_layout_pop(c);
}

static void level_panel_draw(UiCanvasComp* c, DevLevelContext* ctx) {
  const String title = fmt_write_scratch("{} Level Panel", fmt_ui_shape(Globe));
  ui_panel_begin(
      c,
      &ctx->panelComp->panel,
      .title       = title,
      .tabNames    = g_levelTabNames,
      .tabCount    = DevLevelTab_Count,
      .topBarColor = ui_color(100, 0, 0, 192));

  switch (ctx->panelComp->panel.activeTab) {
  case DevLevelTab_Manage:
    manage_panel_draw(c, ctx);
    break;
  case DevLevelTab_Settings:
    if (!ecs_entity_valid(scene_level_asset(ctx->levelManager))) {
      ui_label(c, string_lit("< No loaded level >"), .align = UiAlign_MiddleCenter);
      break;
    }
    if (scene_level_mode(ctx->levelManager) != SceneLevelMode_Edit) {
      ui_label(c, string_lit("< Level not open for edit >"), .align = UiAlign_MiddleCenter);
      break;
    }
    settings_panel_draw(c, ctx);
    break;
  }

  ui_panel_end(c, &ctx->panelComp->panel);
}

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_write(DevFinderComp);
  ecs_access_write(SceneLevelManagerComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevLevelPanelComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DevLevelPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DevLevelUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneLevelManagerComp*  levelManager = ecs_view_write_t(globalItr, SceneLevelManagerComp);
  DevFinderComp*          finder       = ecs_view_write_t(globalItr, DevFinderComp);
  const InputManagerComp* input        = ecs_view_read_t(globalItr, InputManagerComp);

  EcsView* cameraView = ecs_world_view_t(world, CameraView);
  EcsView* panelView  = ecs_world_view_t(world, PanelUpdateView);

  if (input_triggered_lit(input, "SaveLevel")) {
    const EcsEntityId currentLevelAsset = scene_level_asset(levelManager);
    if (currentLevelAsset && scene_level_mode(levelManager) == SceneLevelMode_Edit) {
      scene_level_save(world, currentLevelAsset);
    }
  }

  bool refreshLevels = false;

  EcsIterator* cameraItr = ecs_view_itr(cameraView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DevLevelPanelComp* panelComp = ecs_view_write_t(itr, DevLevelPanelComp);
    UiCanvasComp*      canvas    = ecs_view_write_t(itr, UiCanvasComp);

    DevLevelContext ctx = {
        .world        = world,
        .panelComp    = panelComp,
        .levelManager = levelManager,
        .finder       = finder,
    };

    ecs_view_itr_reset(cameraItr);

    // NOTE: Detached panels have no camera on the window; in that case use the first found camera.
    if (ecs_view_maybe_jump(cameraItr, panelComp->window) || ecs_view_walk(cameraItr)) {
      ctx.cameraTrans = ecs_view_read_t(cameraItr, SceneTransformComp);
    }

    refreshLevels |= (panelComp->flags & DevLevelFlags_RefreshLevels) != 0;
    if (panelComp->flags & DevLevelFlags_Edit) {
      scene_level_reload(world, SceneLevelMode_Edit);
    }
    if (panelComp->flags & DevLevelFlags_Play) {
      scene_level_reload(world, SceneLevelMode_Play);
    }
    if (panelComp->flags & DevLevelFlags_Unload) {
      scene_level_unload(world);
    }
    if (panelComp->flags & DevLevelFlags_Save) {
      scene_level_save(world, scene_level_asset(levelManager));
    }
    panelComp->flags &= ~DevLevelFlags_Volatile;

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp)) && !pinned) {
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

  dev_finder_query(finder, DevFinder_Level, refreshLevels);
}

ecs_module_init(dev_level_module) {
  ecs_register_comp(DevLevelPanelComp, .destructor = ecs_destruct_level_panel);

  ecs_register_view(CameraView);
  ecs_register_view(PanelUpdateGlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DevLevelUpdatePanelSys,
      ecs_view_id(CameraView),
      ecs_view_id(PanelUpdateGlobalView),
      ecs_view_id(PanelUpdateView));
}

EcsEntityId
dev_level_panel_open(EcsWorld* world, const EcsEntityId window, const DevPanelType type) {
  const EcsEntityId  panelEntity = dev_panel_create(world, window, type);
  DevLevelPanelComp* levelPanel  = ecs_world_add_t(
      world,
      panelEntity,
      DevLevelPanelComp,
      .flags      = DevLevelFlags_Default,
      .window     = window,
      .idFilter   = dynstring_create(g_allocHeap, 32),
      .nameBuffer = dynstring_create(g_allocHeap, 32),
      .panel      = ui_panel(.position = ui_vector(0.5f, 0.5f), .size = ui_vector(500, 300)));

  if (type == DevPanelType_Detached) {
    ui_panel_maximize(&levelPanel->panel);
  }

  return panelEntity;
}
