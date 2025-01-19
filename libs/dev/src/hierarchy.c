#include "core_format.h"
#include "dev_hierarchy.h"
#include "dev_panel.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_scrollview.h"
#include "ui_shape.h"
#include "ui_table.h"
#include "ui_widget.h"

ecs_comp_define(DevHierarchyPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
};

static void ecs_destruct_hierarchy_panel(void* data) {
  DevHierarchyPanelComp* comp = data;
  (void)comp;
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevHierarchyPanelComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DevHierarchyPanelComp);
  ecs_access_write(UiCanvasComp);
}

static UiColor hierarchy_bg_color(const u32 index) {
  (void)index;
  return ui_color(48, 48, 48, 192);
}

static void hierarchy_panel_draw(UiCanvasComp* canvas, DevHierarchyPanelComp* panelComp) {
  const String title = fmt_write_scratch("{} Hierarchy Panel", fmt_ui_shape(Tree));
  ui_panel_begin(
      canvas, &panelComp->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  const u32 numEntries = 42;
  const f32 height     = ui_table_height(&table, numEntries);
  ui_scrollview_begin(canvas, &panelComp->scrollview, UiLayer_Normal, height);

  ui_canvas_id_block_next(canvas); // Start the list of entities on its own id block.
  for (u32 i = 0; i != numEntries; ++i) {
    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, hierarchy_bg_color(i));
    ui_label(canvas, fmt_write_scratch("Entity {}", fmt_int(i)), .selectable = true);
  }
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DevHierarchyUpdatePanelSys) {
  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId      entity    = ecs_view_entity(itr);
    DevHierarchyPanelComp* panelComp = ecs_view_write_t(itr, DevHierarchyPanelComp);
    UiCanvasComp*          canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp)) && !pinned) {
      continue;
    }
    hierarchy_panel_draw(canvas, panelComp);

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(dev_hierarchy_module) {
  ecs_register_comp(DevHierarchyPanelComp, .destructor = ecs_destruct_hierarchy_panel);

  ecs_register_system(DevHierarchyUpdatePanelSys, ecs_register_view(PanelUpdateView));
}

EcsEntityId
dev_hierarchy_panel_open(EcsWorld* world, const EcsEntityId window, const DevPanelType type) {
  const EcsEntityId      panelEntity    = dev_panel_create(world, window, type);
  DevHierarchyPanelComp* hierarchyPanel = ecs_world_add_t(
      world,
      panelEntity,
      DevHierarchyPanelComp,
      .panel      = ui_panel(.position = ui_vector(1.0f, 0.0f), .size = ui_vector(500, 350)),
      .scrollview = ui_scrollview());

  if (type == DevPanelType_Detached) {
    ui_panel_maximize(&hierarchyPanel->panel);
  }

  return panelEntity;
}
