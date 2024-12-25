#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "debug_asset.h"
#include "ecs_entity.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_scrollview.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_table.h"
#include "ui_widget.h"

// clang-format off

static const String g_tooltipFilter = string_static("Filter assets by identifier or entity.\nSupports glob characters \a.b*\ar and \a.b?\ar (\a.b!\ar prefix to invert).");
static const String g_tooltipReload = string_static("Request the asset to be reloaded.\nReload is delayed until all systems release the asset and reacquire it.");

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
  EcsEntityId      entity;
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
  u32                countLoaded;
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
  ecs_view_flags(EcsViewFlags_Exclusive); // DebugAssetPanelComp's are exclusively managed here.

  ecs_access_read(DebugPanelComp);
  ecs_access_write(DebugAssetPanelComp);
  ecs_access_write(UiCanvasComp);
}

static bool asset_filter(DebugAssetPanelComp* panel, const AssetComp* asset, const EcsEntityId e) {
  if (!panel->idFilter.size) {
    return true;
  }
  const String           assetId   = asset_id(asset);
  const String           rawFilter = dynstring_view(&panel->idFilter);
  const String           filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));
  const StringMatchFlags flags     = StringMatchFlags_IgnoreCase;
  if (string_match_glob(assetId, filter, flags)) {
    return true;
  }
  return string_match_glob(fmt_write_scratch("{}", ecs_entity_fmt(e)), filter, flags);
}

static void asset_info_query(DebugAssetPanelComp* panelComp, EcsWorld* world) {
  dynarray_clear(&panelComp->assets);
  panelComp->countLoaded = 0;

  EcsView* assetView = ecs_world_view_t(world, AssetView);
  for (EcsIterator* itr = ecs_view_itr(assetView); ecs_view_walk(itr);) {
    const EcsEntityId entity    = ecs_view_entity(itr);
    const AssetComp*  assetComp = ecs_view_read_t(itr, AssetComp);

    if (!asset_filter(panelComp, assetComp, entity)) {
      continue;
    }

    DebugAssetStatus status;
    if (ecs_world_has_t(world, entity, AssetFailedComp)) {
      status = DebugAssetStatus_Failed;
    } else if (asset_is_loading(assetComp)) {
      status = DebugAssetStatus_Loading;
    } else if (ecs_world_has_t(world, entity, AssetChangedComp)) {
      status = DebugAssetStatus_Changed;
    } else if (ecs_world_has_t(world, entity, AssetLoadedComp)) {
      ++panelComp->countLoaded;
      status = asset_ref_count(assetComp) ? DebugAssetStatus_LoadedReferenced
                                          : DebugAssetStatus_LoadedUnreferenced;
    } else {
      status = DebugAssetStatus_Idle;
    }

    *dynarray_push_t(&panelComp->assets, DebugAssetInfo) = (DebugAssetInfo){
        .id               = asset_id(assetComp),
        .entity           = entity,
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
  ui_style_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 60);
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Filter:"));
  ui_table_next_column(canvas, &table);
  ui_textbox(
      canvas, &panelComp->idFilter, .placeholder = string_lit("*"), .tooltip = g_tooltipFilter);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Sort:"));
  ui_table_next_column(canvas, &table);
  ui_select(canvas, (i32*)&panelComp->sortMode, g_sortModeNames, DebugAssetSortMode_Count);

  const String stats = fmt_write_scratch(
      "Count: {}, Loaded: {}",
      fmt_int(panelComp->assets.size, .minDigits = 4),
      fmt_int(panelComp->countLoaded, .minDigits = 4));

  ui_table_next_column(canvas, &table);
  ui_style_variation(canvas, UiVariation_Monospace);
  ui_label(canvas, stats, .selectable = true);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
}

static void
asset_panel_draw_reload(UiCanvasComp* canvas, const DebugAssetInfo* asset, EcsWorld* world) {
  ui_layout_push(canvas);
  ui_layout_move_to(canvas, UiBase_Current, UiAlign_BottomRight, Ui_X);
  ui_layout_resize(canvas, UiAlign_BottomRight, ui_vector(25, 0), UiBase_Absolute, Ui_X);
  if (ui_button(canvas, .label = string_lit("R"), .fontSize = 14, .tooltip = g_tooltipReload)) {
    asset_reload_request(world, asset->entity);
  }
  ui_layout_pop(canvas);
}

static void
asset_panel_draw(UiCanvasComp* canvas, DebugAssetPanelComp* panelComp, EcsWorld* world) {

  const String title = fmt_write_scratch("{} Asset Panel", fmt_ui_shape(Storage));
  ui_panel_begin(
      canvas, &panelComp->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  asset_options_draw(canvas, panelComp);

  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None, UiLayer_Normal);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 320);
  ui_table_add_column(&table, UiTableColumn_Fixed, 180);
  ui_table_add_column(&table, UiTableColumn_Fixed, 90);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Id"), string_lit("Asset identifier.")},
          {string_lit("Entity"), string_lit("Entity identifier of the asset.")},
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
    ui_label_entity(canvas, asset->entity);
    ui_table_next_column(canvas, &table);
    ui_label(canvas, g_statusNames[asset->status]);
    ui_table_next_column(canvas, &table);
    ui_label(canvas, asset->dirty ? string_lit("y") : string_lit("n"));
    asset_panel_draw_reload(canvas, asset, world);

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

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (debug_panel_hidden(ecs_view_read_t(itr, DebugPanelComp)) && !pinned) {
      continue;
    }
    asset_info_query(panelComp, world);
    asset_panel_draw(canvas, panelComp, world);

    if (ui_panel_closed(&panelComp->panel)) {
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

EcsEntityId
debug_asset_panel_open(EcsWorld* world, const EcsEntityId window, const DebugPanelType type) {
  const EcsEntityId    panelEntity = debug_panel_create(world, window, type);
  DebugAssetPanelComp* assetPanel  = ecs_world_add_t(
      world,
      panelEntity,
      DebugAssetPanelComp,
      .panel      = ui_panel(.size = ui_vector(950, 500)),
      .scrollview = ui_scrollview(),
      .idFilter   = dynstring_create(g_allocHeap, 32),
      .sortMode   = DebugAssetSortMode_Status,
      .assets     = dynarray_create_t(g_allocHeap, DebugAssetInfo, 256));

  if (type == DebugPanelType_Detached) {
    ui_panel_maximize(&assetPanel->panel);
  }

  return panelEntity;
}
