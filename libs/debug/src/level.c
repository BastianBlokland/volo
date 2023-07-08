#include "asset_manager.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "scene_level.h"
#include "ui.h"

// clang-format off

static const String g_tooltipFilter     = string_static("Filter levels by identifier.\nSupports glob characters \a.b*\ar and \a.b?\ar.");
static const String g_levelQueryPattern = string_static("levels/*.lvl");

// clang-format on

typedef enum {
  DebugLevelFlags_RefreshRequired = 1 << 0,

  DebugLevelFlags_None    = 0,
  DebugLevelFlags_Default = DebugLevelFlags_RefreshRequired,
} DebugLevelFlags;

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

static void
level_assets_refresh(EcsWorld* world, AssetManagerComp* assets, DebugLevelPanelComp* panelComp) {
  EcsEntityId assetEntities[asset_query_max_results];
  const u32   assetCount = asset_query(world, assets, g_levelQueryPattern, assetEntities);

  dynarray_clear(&panelComp->levelAssets);
  for (u32 i = 0; i != assetCount; ++i) {
    *dynarray_push_t(&panelComp->levelAssets, EcsEntityId) = assetEntities[i];
  }
}

static bool level_id_filter(DebugLevelPanelComp* panelComp, const String levelId) {
  if (!panelComp->idFilter.size) {
    return true;
  }
  const String rawFilter = dynstring_view(&panelComp->idFilter);
  const String filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));
  return string_match_glob(levelId, filter, StringMatchFlags_IgnoreCase);
}

static void level_panel_options_draw(UiCanvasComp* canvas, DebugLevelPanelComp* panelComp) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(5, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 60);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Filter:"));
  ui_table_next_column(canvas, &table);
  ui_textbox(
      canvas, &panelComp->idFilter, .placeholder = string_lit("*"), .tooltip = g_tooltipFilter);

  ui_layout_pop(canvas);
}

static void level_panel_draw(
    UiCanvasComp*                canvas,
    const SceneLevelManagerComp* levelManager,
    DebugLevelPanelComp*         panelComp,
    EcsView*                     assetView) {
  const String title = fmt_write_scratch("{} Level Panel", fmt_ui_shape(Globe));
  ui_panel_begin(
      canvas, &panelComp->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  level_panel_options_draw(canvas, panelComp);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  const bool isLoading = scene_level_is_loading(levelManager);
  const bool disabled  = isLoading;
  ui_style_push(canvas);
  if (disabled) {
    ui_style_color_mult(canvas, 0.5f);
  }

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Level"), string_lit("Level identifier.")},
          {string_lit("Actions"), string_empty},
      });

  const f32 totalHeight = ui_table_height(&table, panelComp->totalRows);
  ui_scrollview_begin(canvas, &panelComp->scrollview, totalHeight);
  panelComp->totalRows = 0;

  EcsIterator* assetItr = ecs_view_itr(assetView);
  dynarray_for_t(&panelComp->levelAssets, EcsEntityId, levelAsset) {
    if (!ecs_view_maybe_jump(assetItr, *levelAsset)) {
      continue;
    }
    const String id = asset_id(ecs_view_read_t(assetItr, AssetComp));
    if (!level_id_filter(panelComp, id)) {
      continue;
    }
    ++panelComp->totalRows;

    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));

    ui_label(canvas, id, .selectable = true);
    ui_table_next_column(canvas, &table);
  }

  ui_scrollview_end(canvas, &panelComp->scrollview);

  ui_style_pop(canvas);
  ui_layout_container_pop(canvas);
  ui_panel_end(canvas, &panelComp->panel);
}

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_read(SceneLevelManagerComp);
  ecs_access_write(AssetManagerComp);
}

ecs_view_define(PanelUpdateView) {
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
  const SceneLevelManagerComp* levelManager = ecs_view_read_t(globalItr, SceneLevelManagerComp);

  EcsView* assetView = ecs_world_view_t(world, AssetView);
  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);

  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugLevelPanelComp* panelComp = ecs_view_write_t(itr, DebugLevelPanelComp);
    UiCanvasComp*        canvas    = ecs_view_write_t(itr, UiCanvasComp);

    if (panelComp->flags & DebugLevelFlags_RefreshRequired) {
      level_assets_refresh(world, assets, panelComp);
      panelComp->flags &= ~DebugLevelFlags_RefreshRequired;
    }

    ui_canvas_reset(canvas);
    level_panel_draw(canvas, levelManager, panelComp, assetView);

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
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);

  ecs_world_add_t(
      world,
      panelEntity,
      DebugLevelPanelComp,
      .flags       = DebugLevelFlags_Default,
      .idFilter    = dynstring_create(g_alloc_heap, 32),
      .levelAssets = dynarray_create_t(g_alloc_heap, EcsEntityId, 8),
      .panel       = ui_panel(.position = ui_vector(0.75f, 0.5f), .size = ui_vector(375, 250)));
  return panelEntity;
}
