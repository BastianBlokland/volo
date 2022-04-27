#include "asset_manager.h"
#include "core_alloc.h"
#include "debug_asset.h"
#include "ecs_world.h"
#include "ui.h"

typedef struct {
  String id;
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
  return compare_string(field_ptr(a, DebugAssetInfo, id), field_ptr(b, DebugAssetInfo, id));
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
    const AssetComp* assetComp                           = ecs_view_read_t(itr, AssetComp);
    *dynarray_push_t(&panelComp->assets, DebugAssetInfo) = (DebugAssetInfo){
        .id = asset_id(assetComp),
    };
  }

  dynarray_sort(&panelComp->assets, compare_asset_info);
}

static void asset_panel_draw(UiCanvasComp* canvas, DebugAssetPanelComp* panelComp) {
  const String title = fmt_write_scratch("{} Asset Debug", fmt_ui_shape(Storage));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  const u32 numAssets = (u32)panelComp->assets.size;
  ui_scrollview_begin(canvas, &panelComp->scrollview, ui_table_height(&table, numAssets));

  dynarray_for_t(&panelComp->assets, DebugAssetInfo, asset) {
    ui_table_next_row(canvas, &table);
    ui_label(canvas, asset->id);
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
      .panel      = ui_panel(ui_vector(500, 400)),
      .scrollview = ui_scrollview(),
      .assets     = dynarray_create_t(g_alloc_heap, DebugAssetInfo, 256));
  return panelEntity;
}
