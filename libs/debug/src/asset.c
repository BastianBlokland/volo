#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "debug_asset.h"
#include "ecs_world.h"
#include "ui.h"

typedef enum {
  DebugAssetStatus_Idle,
  DebugAssetStatus_Changed,
  DebugAssetStatus_LoadedUnreferenced,
  DebugAssetStatus_LoadedReferenced,
  DebugAssetStatus_Loading,
  DebugAssetStatus_Failed,

  DebugAssetStatus_Count,
} DebugAssetStatus;

typedef struct {
  String           id;
  DebugAssetStatus status;
  u32              refCount, loadCount;
} DebugAssetInfo;

ecs_comp_define(DebugAssetPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
  DynArray     assets; // DebugAssetInfo[]
};

static void ecs_destruct_asset_panel(void* data) {
  DebugAssetPanelComp* comp = data;
  dynarray_destroy(&comp->assets);
}

static i8 compare_asset_info(const void* a, const void* b) {
  const DebugAssetInfo* assetA = a;
  const DebugAssetInfo* assetB = b;

  i8 statusOrder = compare_u32_reverse(&assetA->status, &assetB->status);
  if (!statusOrder) {
    statusOrder = compare_string(&assetA->id, &assetB->id);
  }
  return statusOrder;
}

static String debug_asset_status_str(const DebugAssetStatus status) {
  static const String g_names[] = {
      string_static("Idle"),
      string_static("Changed"),
      string_static("Loaded"),
      string_static("Loaded"),
      string_static("Loading"),
      string_static("Failed"),
  };
  ASSERT(array_elems(g_names) == DebugAssetStatus_Count, "Incorrect number of names");
  return g_names[status];
}

ecs_view_define(AssetView) { ecs_access_read(AssetComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugAssetPanelComp);
  ecs_access_write(UiCanvasComp);
}

static void asset_info_query(DebugAssetPanelComp* panelComp, EcsWorld* world) {
  dynarray_clear(&panelComp->assets);

  EcsView* assetView = ecs_world_view_t(world, AssetView);
  for (EcsIterator* itr = ecs_view_itr(assetView); ecs_view_walk(itr);) {
    const EcsEntityId entity    = ecs_view_entity(itr);
    const AssetComp*  assetComp = ecs_view_read_t(itr, AssetComp);

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
        .id        = asset_id(assetComp),
        .status    = status,
        .refCount  = asset_ref_count(assetComp),
        .loadCount = asset_load_count(assetComp),
    };
  }

  dynarray_sort(&panelComp->assets, compare_asset_info);
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

static void asset_panel_draw(UiCanvasComp* canvas, DebugAssetPanelComp* panelComp) {
  const String title = fmt_write_scratch("{} Asset Debug", fmt_ui_shape(Storage));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 350);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Flexible, 75);

  const u32 numAssets = (u32)panelComp->assets.size;
  ui_scrollview_begin(canvas, &panelComp->scrollview, ui_table_height(&table, numAssets));

  dynarray_for_t(&panelComp->assets, DebugAssetInfo, asset) {
    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, asset_info_bg_color(asset));

    ui_label(canvas, asset->id);
    ui_table_next_column(canvas, &table);
    ui_label(canvas, debug_asset_status_str(asset->status));
    ui_table_next_column(canvas, &table);
    if (asset->refCount) {
      ui_label(canvas, fmt_write_scratch("refs: {}", fmt_int(asset->refCount)));
    }
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("loads: {}", fmt_int(asset->loadCount)));
  }

  ui_scrollview_end(canvas, &panelComp->scrollview);
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
      .panel      = ui_panel(ui_vector(750, 500)),
      .scrollview = ui_scrollview(),
      .assets     = dynarray_create_t(g_alloc_heap, DebugAssetInfo, 256));
  return panelEntity;
}
