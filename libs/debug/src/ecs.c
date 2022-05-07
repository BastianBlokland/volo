#include "core_alloc.h"
#include "debug_ecs.h"
#include "debug_register.h"
#include "ecs_world.h"
#include "ui.h"

typedef struct {
  EcsCompId id;
  String    name;
  u32       size, align;
  u32       numArchetypes;
} DebugEcsCompInfo;

ecs_comp_define(DebugEcsPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
  DynArray     components; // DebugEcsCompInfo[]
};

static void ecs_destruct_ecs_panel(void* data) {
  DebugEcsPanelComp* comp = data;
  dynarray_destroy(&comp->components);
}

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugEcsPanelComp);
  ecs_access_write(UiCanvasComp);
}

static void comp_info_query(DebugEcsPanelComp* panelComp, EcsWorld* world) {
  dynarray_clear(&panelComp->components);

  const EcsDef* def = ecs_world_def(world);
  for (EcsCompId id = 0; id != ecs_def_comp_count(def); ++id) {
    *dynarray_push_t(&panelComp->components, DebugEcsCompInfo) = (DebugEcsCompInfo){
        .id            = id,
        .name          = ecs_def_comp_name(def, id),
        .size          = (u32)ecs_def_comp_size(def, id),
        .align         = (u32)ecs_def_comp_align(def, id),
        .numArchetypes = ecs_world_archetype_count_with_comp(world, id),
    };
  }
}

static void physics_panel_draw(UiCanvasComp* canvas, DebugEcsPanelComp* panelComp) {
  const String title = fmt_write_scratch("{} Ecs Debug", fmt_ui_shape(Extension));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 300);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Id"), string_lit("Component identifier.")},
          {string_lit("Name"), string_lit("Component name.")},
          {string_lit("Size"), string_lit("Component size (in bytes).")},
          {string_lit("Align"), string_lit("Component required minimum alignment (in bytes).")},
          {string_lit("Archetypes"), string_lit("Number of archetypes with this component.")},
      });

  const u32 numComps = (u32)panelComp->components.size;
  ui_scrollview_begin(canvas, &panelComp->scrollview, ui_table_height(&table, numComps));

  dynarray_for_t(&panelComp->components, DebugEcsCompInfo, compInfo) {
    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));

    ui_label(canvas, fmt_write_scratch("{}", fmt_int(compInfo->id)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, compInfo->name, .selectable = true);
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(compInfo->size)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(compInfo->align)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(compInfo->numArchetypes)));
  }

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugPhysicsUpdatePanelSys) {
  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId  entity    = ecs_view_entity(itr);
    DebugEcsPanelComp* panelComp = ecs_view_write_t(itr, DebugEcsPanelComp);
    UiCanvasComp*      canvas    = ecs_view_write_t(itr, UiCanvasComp);

    comp_info_query(panelComp, world);

    ui_canvas_reset(canvas);
    physics_panel_draw(canvas, panelComp);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_ecs_module) {
  ecs_register_comp(DebugEcsPanelComp, .destructor = ecs_destruct_ecs_panel);

  ecs_register_view(PanelUpdateView);

  ecs_register_system(DebugPhysicsUpdatePanelSys, ecs_view_id(PanelUpdateView));
}

EcsEntityId debug_ecs_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugEcsPanelComp,
      .panel      = ui_panel(ui_vector(800, 500)),
      .components = dynarray_create_t(g_alloc_heap, DebugEcsCompInfo, 256));
  return panelEntity;
}
