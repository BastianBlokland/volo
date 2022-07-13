#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "debug_asset.h"
#include "ecs_world.h"
#include "ui.h"

// clang-format off

static const String g_tooltipFilter = string_static("Filter assets by identifier.\nSupports glob characters \a.b*\ar and \a.b?\ar.");

// clang-format on

typedef enum {
  DebugAssetStatus_Idle,
  DebugAssetStatus_Changed,
  DebugAssetStatus_LoadedUnreferenced,
  DebugAssetStatus_LoadedReferenced,
  DebugAssetStatus_Loading,
  DebugAssetStatus_Failed,

  DebugAssetStatus_Count,
} DebugAssetStatus;

typedef enum {
  DebugAssetSortMode_Id,
  DebugAssetSortMode_Status,

  DebugAssetSortMode_Count,
} DebugAssetSortMode;

typedef struct {
  String           id;
  DebugAssetStatus status;
  bool             dirty;
  u32              refCount, loadCount, ticksUntilUnload;
} DebugAssetInfo;

static const String g_statusNames[] = {
    string_static("Idle"),
    string_static("Changed"),
    string_static("Loaded"),
    string_static("Loaded"),
    string_static("Loading"),
    string_static("Failed"),
};
ASSERT(array_elems(g_statusNames) == DebugAssetStatus_Count, "Incorrect number of names");

static const String g_sortModeNames[] = {
    string_static("Id"),
    string_static("Status"),
};
ASSERT(array_elems(g_sortModeNames) == DebugAssetSortMode_Count, "Incorrect number of names");

ecs_comp_define(DebugAssetPanelComp) {
  UiPanel            panel;
  UiScrollview       scrollview;
  DynString          idFilter;
  DebugAssetSortMode sortMode;
  DynArray           assets; // DebugAssetInfo[]
};

static void ecs_destruct_asset_panel(void* data) {
  DebugAssetPanelComp* comp = data;
  dynstring_destroy(&comp->idFilter);
  dynarray_destroy(&comp->assets);
}

static i8 compare_asset_info_id(const void* a, const void* b) {
  const DebugAssetInfo* assetA = a;
  const DebugAssetInfo* assetB = b;

  return compare_string(&assetA->id, &assetB->id);
}

static i8 compare_asset_info_status(const void* a, const void* b) {
  const DebugAssetInfo* assetA = a;
  const DebugAssetInfo* assetB = b;

  i8 statusOrder = compare_u32_reverse(&assetA->status, &assetB->status);
  if (!statusOrder) {
    statusOrder = compare_string(&assetA->id, &assetB->id);
  }
  return statusOrder;
}

ecs_view_define(AssetView) { ecs_access_read(AssetComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugAssetPanelComp);
  ecs_access_write(UiCanvasComp);
}

static bool asset_filter(DebugAssetPanelComp* panelComp, const AssetComp* assetComp) {
  if (string_is_empty(panelComp->idFilter)) {
    return true;
  }
  const String assetId   = asset_id(assetComp);
  const String rawFilter = dynstring_view(&panelComp->idFilter);
  const String filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));
  return string_match_glob(assetId, filter, StringMatchFlags_IgnoreCase);
}

static void asset_info_query(DebugAssetPanelComp* panelComp, EcsWorld* world) {
  dynarray_clear(&panelComp->assets);

  EcsView* assetView = ecs_world_view_t(world, AssetView);
  for (EcsIterator* itr = ecs_view_itr(assetView); ecs_view_walk(itr);) {
    const EcsEntityId entity    = ecs_view_entity(itr);
    const AssetComp*  assetComp = ecs_view_read_t(itr, AssetComp);

    if (!asset_filter(panelComp, assetComp)) {
      continue;
    }

    DebugAssetStatus status;
    if (ecs_world_has_t(world, entity, AssetFailedComp)) {
      status = DebugAssetStatus_Failed;
    } else if (asset_is_loading(assetComp)) {
      status = DebugAssetStatus_Loading;
    } else if (ecs_world_has_t(world, entity, AssetLoadedComp)) {
      status = asset_ref_count(assetComp) ? DebugAssetStatus_LoadedReferenced
                                          : DebugAssetStatus_LoadedUnreferenced;
    } else if (ecs_world_has_t(world, entity, AssetChangedComp)) {
      status = DebugAssetStatus_Changed;
    } else {
      status = DebugAssetStatus_Idle;
    }

    *dynarray_push_t(&panelComp->assets, DebugAssetInfo) = (DebugAssetInfo){
        .id               = asset_id(assetComp),
        .status           = status,
        .dirty            = ecs_world_has_t(world, entity, AssetDirtyComp),
        .refCount         = asset_ref_count(assetComp),
        .loadCount        = asset_load_count(assetComp),
        .ticksUntilUnload = asset_ticks_until_unload(assetComp),
    };
  }

  switch (panelComp->sortMode) {
  case DebugAssetSortMode_Id:
    dynarray_sort(&panelComp->assets, compare_asset_info_id);
    break;
  case DebugAssetSortMode_Status:
    dynarray_sort(&panelComp->assets, compare_asset_info_status);
    break;
  case DebugAssetSortMode_Count:
    break;
  }
}

static UiColor asset_info_bg_color(const DebugAssetInfo* asset) {
  switch (asset->status) {
  case DebugAssetStatus_Idle:
    return ui_color(48, 48, 48, 192);
  case DebugAssetStatus_Changed:
    return ui_color(48, 48, 16, 192);
  case DebugAssetStatus_LoadedReferenced:
    return ui_color(16, 64, 16, 192);
  case DebugAssetStatus_LoadedUnreferenced:
    return ui_color(16, 16, 64, 192);
  case DebugAssetStatus_Loading:
    return ui_color(16, 64, 64, 192);
  case DebugAssetStatus_Failed:
    return ui_color(64, 16, 16, 192);
  case DebugAssetStatus_Count:
    break;
  }
  diag_crash();
}

static void asset_options_draw(UiCanvasComp* canvas, DebugAssetPanelComp* panelComp) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 60);
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Filter:"));
  ui_table_next_column(canvas, &table);
  ui_textbox(
      canvas, &panelComp->idFilter, .placeholder = string_lit("*"), .tooltip = g_tooltipFilter);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Sort:"));
  ui_table_next_column(canvas, &table);
  ui_select(canvas, (i32*)&panelComp->sortMode, g_sortModeNames, DebugAssetSortMode_Count);

  ui_layout_pop(canvas);
}

static void asset_panel_draw(UiCanvasComp* canvas, DebugAssetPanelComp* panelComp) {
  const String title = fmt_write_scratch("{} Asset Debug", fmt_ui_shape(Storage));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  asset_options_draw(canvas, panelComp);

  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 350);
  ui_table_add_column(&table, UiTableColumn_Fixed, 90);
  ui_table_add_column(&table, UiTableColumn_Fixed, 70);
  ui_table_add_column(&table, UiTableColumn_Fixed, 70);
  ui_table_add_column(&table, UiTableColumn_Fixed, 70);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Id"), string_lit("Asset identifier.")},
          {string_lit("Status"), string_lit("Current asset status.")},
          {string_lit("Dirty"), string_lit("Does the asset need processing at this time.")},
          {string_lit("Refs"), string_lit("Current reference counter.")},
          {string_lit("Loads"), string_lit("How many times has this asset been loaded.")},
          {string_lit("Unload delay"),
           string_lit("How many ticks until this asset will be unloaded.")},
      });

  const u32 numAssets = (u32)panelComp->assets.size;
  ui_scrollview_begin(canvas, &panelComp->scrollview, ui_table_height(&table, numAssets));

  ui_canvas_id_block_next(canvas); // Start the list of assets on its own id block.
  dynarray_for_t(&panelComp->assets, DebugAssetInfo, asset) {
    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, asset_info_bg_color(asset));

    ui_canvas_id_block_string(canvas, asset->id); // Set a stable id based on the asset id.

    ui_label(canvas, asset->id, .selectable = true);
    ui_table_next_column(canvas, &table);
    ui_label(canvas, g_statusNames[asset->status]);
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_bool(asset->dirty)));
    ui_table_next_column(canvas, &table);
    if (asset->refCount) {
      ui_label(canvas, fmt_write_scratch("{}", fmt_int(asset->refCount)));
    }
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(asset->loadCount)));
    ui_table_next_column(canvas, &table);
    if (asset->status == DebugAssetStatus_LoadedUnreferenced) {
      ui_label(canvas, fmt_write_scratch("{}", fmt_int(asset->ticksUntilUnload)));
    }
  }
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugAssetUpdatePanelSys) {
  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId    entity    = ecs_view_entity(itr);
    DebugAssetPanelComp* panelComp = ecs_view_write_t(itr, DebugAssetPanelComp);
    UiCanvasComp*        canvas    = ecs_view_write_t(itr, UiCanvasComp);

    asset_info_query(panelComp, world);

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
  ecs_register_comp(DebugAssetPanelComp, .destructor = ecs_destruct_asset_panel);

  ecs_register_view(PanelUpdateView);
  ecs_register_view(AssetView);

  ecs_register_system(
      DebugAssetUpdatePanelSys, ecs_view_id(PanelUpdateView), ecs_view_id(AssetView));
}

EcsEntityId debug_asset_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugAssetPanelComp,
      .panel      = ui_panel(ui_vector(850, 500)),
      .scrollview = ui_scrollview(),
      .idFilter   = dynstring_create(g_alloc_heap, 32),
      .sortMode   = DebugAssetSortMode_Status,
      .assets     = dynarray_create_t(g_alloc_heap, DebugAssetInfo, 256));
  return panelEntity;
}
