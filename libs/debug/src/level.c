#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "debug_panel.h"
#include "ecs_world.h"
#include "input_manager.h"
#include "scene_level.h"
#include "ui.h"

// clang-format off

static const String g_tooltipReload     = string_static("Reload the current level.");
static const String g_tooltipUnload     = string_static("Unload the current level.");
static const String g_tooltipSave       = string_static("Save the current level.");
static const String g_tooltipFilter     = string_static("Filter levels by identifier.\nSupports glob characters \a.b*\ar and \a.b?\ar.");
static const String g_levelQueryPattern = string_static("levels/*.level");

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

  DebugLevelTab_Count,
} DebugLevelTab;

static const String g_levelTabNames[] = {
    string_static("Manage"),
};
ASSERT(array_elems(g_levelTabNames) == DebugLevelTab_Count, "Incorrect number of names");

ecs_comp_define(DebugLevelPanelComp) {
  DebugLevelFlags flags;
  DynString       idFilter;
  DynArray        levelAssets; // EcsEntityId[]
  UiPanel         panel;
  UiScrollview    scrollview;
  u32             totalRows;
};

static void ecs_destruct_level_panel(void* data) {
  DebugLevelPanelComp* comp = data;
  dynstring_destroy(&comp->idFilter);
  dynarray_destroy(&comp->levelAssets);
}

ecs_view_define(AssetView) { ecs_access_read(AssetComp); }

typedef struct {
  EcsWorld*                    world;
  DebugLevelPanelComp*         panelComp;
  const SceneLevelManagerComp* levelManager;
  AssetManagerComp*            assets;
} DebugLevelContext;

static void level_assets_refresh(DebugLevelContext* ctx) {
  EcsEntityId assetEntities[asset_query_max_results];
  const u32   assetCount = asset_query(ctx->world, ctx->assets, g_levelQueryPattern, assetEntities);

  dynarray_clear(&ctx->panelComp->levelAssets);
  for (u32 i = 0; i != assetCount; ++i) {
    *dynarray_push_t(&ctx->panelComp->levelAssets, EcsEntityId) = assetEntities[i];
  }
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

static void manage_panel_draw(UiCanvasComp* c, DebugLevelContext* ctx, EcsView* assetView) {
  manage_panel_options_draw(c, ctx);
  ui_layout_grow(c, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(c, UiClip_None);

  const bool isLoading = scene_level_is_loading(ctx->levelManager);
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

  EcsIterator* assetItr = ecs_view_itr(assetView);
  dynarray_for_t(&ctx->panelComp->levelAssets, EcsEntityId, levelAsset) {
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

static void level_panel_draw(UiCanvasComp* c, DebugLevelContext* ctx, EcsView* assetView) {
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
    manage_panel_draw(c, ctx, assetView);
    break;
  }

  ui_panel_end(c, &ctx->panelComp->panel);
}

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(SceneLevelManagerComp);
  ecs_access_write(AssetManagerComp);
}

ecs_view_define(PanelUpdateView) {
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
  AssetManagerComp*            assets       = ecs_view_write_t(globalItr, AssetManagerComp);
  const InputManagerComp*      input        = ecs_view_read_t(globalItr, InputManagerComp);
  const SceneLevelManagerComp* levelManager = ecs_view_read_t(globalItr, SceneLevelManagerComp);

  EcsView* assetView = ecs_world_view_t(world, AssetView);
  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);

  if (input_triggered_lit(input, "SaveLevel")) {
    const EcsEntityId currentLevelAsset = scene_level_asset(levelManager);
    if (currentLevelAsset) {
      scene_level_save(world, currentLevelAsset);
    }
  }

  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugLevelPanelComp* panelComp = ecs_view_write_t(itr, DebugLevelPanelComp);
    UiCanvasComp*        canvas    = ecs_view_write_t(itr, UiCanvasComp);

    DebugLevelContext ctx = {
        .world        = world,
        .panelComp    = panelComp,
        .levelManager = levelManager,
        .assets       = assets,
    };

    if (panelComp->flags & DebugLevelFlags_RefreshAssets) {
      level_assets_refresh(&ctx);
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
    if (debug_panel_hidden(ecs_view_read_t(itr, DebugPanelComp))) {
      continue;
    }
    level_panel_draw(canvas, &ctx, assetView);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
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
  ecs_register_view(PanelUpdateGlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugLevelUpdatePanelSys,
      ecs_view_id(AssetView),
      ecs_view_id(PanelUpdateGlobalView),
      ecs_view_id(PanelUpdateView));
}

EcsEntityId debug_level_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = debug_panel_create(world, window);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugLevelPanelComp,
      .flags       = DebugLevelFlags_Default,
      .idFilter    = dynstring_create(g_alloc_heap, 32),
      .levelAssets = dynarray_create_t(g_alloc_heap, EcsEntityId, 8),
      .panel       = ui_panel(.position = ui_vector(0.5f, 0.5f), .size = ui_vector(500, 300)));
  return panelEntity;
}
