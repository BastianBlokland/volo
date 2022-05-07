#include "core_alloc.h"
#include "core_array.h"
#include "debug_ecs.h"
#include "debug_register.h"
#include "ecs_world.h"
#include "ui.h"

typedef struct {
  EcsCompId id;
  String    name;
  u32       size, align;
  u32       numArchetypes, numEntities;
} DebugEcsCompInfo;

typedef enum {
  DebugCompSortMode_Id,
  DebugCompSortMode_Name,
  DebugCompSortMode_Size,
  DebugCompSortMode_Archetypes,
  DebugCompSortMode_Entities,

  DebugCompSortMode_Count,
} DebugCompSortMode;

static const String g_compSortModeNames[] = {
    string_static("Id"),
    string_static("Name"),
    string_static("Size"),
    string_static("Archetypes"),
    string_static("Entities"),
};
ASSERT(array_elems(g_compSortModeNames) == DebugCompSortMode_Count, "Incorrect number of names");

ecs_comp_define(DebugEcsPanelComp) {
  UiPanel           panel;
  UiScrollview      scrollview;
  DynString         nameFilter;
  DebugCompSortMode compSortMode;
  DynArray          components; // DebugEcsCompInfo[]
};

static void ecs_destruct_ecs_panel(void* data) {
  DebugEcsPanelComp* comp = data;
  dynstring_destroy(&comp->nameFilter);
  dynarray_destroy(&comp->components);
}

static i8 compare_comp_info_name(const void* a, const void* b) {
  return compare_string(field_ptr(a, DebugEcsCompInfo, name), field_ptr(b, DebugEcsCompInfo, name));
}

static i8 compare_comp_info_size(const void* a, const void* b) {
  return compare_u32_reverse(
      field_ptr(a, DebugEcsCompInfo, size), field_ptr(b, DebugEcsCompInfo, size));
}

static i8 compare_comp_info_archetypes(const void* a, const void* b) {
  return compare_u32_reverse(
      field_ptr(a, DebugEcsCompInfo, numArchetypes), field_ptr(b, DebugEcsCompInfo, numArchetypes));
}

static i8 compare_comp_info_entities(const void* a, const void* b) {
  return compare_u32_reverse(
      field_ptr(a, DebugEcsCompInfo, numEntities), field_ptr(b, DebugEcsCompInfo, numEntities));
}

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugEcsPanelComp);
  ecs_access_write(UiCanvasComp);
}

static bool comp_filter(DebugEcsPanelComp* panelComp, const EcsDef* def, const EcsCompId comp) {
  if (string_is_empty(panelComp->nameFilter)) {
    return true;
  }
  const String rawFilter = dynstring_view(&panelComp->nameFilter);
  const String filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));
  return string_match_glob(ecs_def_comp_name(def, comp), filter, StringMatchFlags_IgnoreCase);
}

static void comp_info_query(DebugEcsPanelComp* panelComp, EcsWorld* world) {
  dynarray_clear(&panelComp->components);

  const EcsDef* def = ecs_world_def(world);
  for (EcsCompId id = 0; id != ecs_def_comp_count(def); ++id) {
    if (!comp_filter(panelComp, def, id)) {
      continue;
    }

    *dynarray_push_t(&panelComp->components, DebugEcsCompInfo) = (DebugEcsCompInfo){
        .id            = id,
        .name          = ecs_def_comp_name(def, id),
        .size          = (u32)ecs_def_comp_size(def, id),
        .align         = (u32)ecs_def_comp_align(def, id),
        .numArchetypes = ecs_world_archetype_count_with_comp(world, id),
        .numEntities   = ecs_world_entity_count_with_comp(world, id),
    };
  }

  switch (panelComp->compSortMode) {
  case DebugCompSortMode_Name:
    dynarray_sort(&panelComp->components, compare_comp_info_name);
    break;
  case DebugCompSortMode_Size:
    dynarray_sort(&panelComp->components, compare_comp_info_size);
    break;
  case DebugCompSortMode_Archetypes:
    dynarray_sort(&panelComp->components, compare_comp_info_archetypes);
    break;
  case DebugCompSortMode_Entities:
    dynarray_sort(&panelComp->components, compare_comp_info_entities);
    break;
  case DebugCompSortMode_Id:
  case DebugCompSortMode_Count:
    break;
  }
}

static UiColor comp_info_bg_color(const DebugEcsCompInfo* compInfo) {
  return compInfo->numEntities ? ui_color(16, 64, 16, 192) : ui_color(48, 48, 48, 192);
}

static void comp_options_draw(UiCanvasComp* canvas, DebugEcsPanelComp* panelComp) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Filter:"));
  ui_table_next_column(canvas, &table);
  ui_textbox(canvas, &panelComp->nameFilter, .placeholder = string_lit("*"));
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Sort:"));
  ui_table_next_column(canvas, &table);
  ui_select(canvas, (i32*)&panelComp->compSortMode, g_compSortModeNames, DebugCompSortMode_Count);

  ui_layout_pop(canvas);
}

static void physics_panel_draw(UiCanvasComp* canvas, DebugEcsPanelComp* panelComp) {
  const String title = fmt_write_scratch("{} Ecs Debug", fmt_ui_shape(Extension));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  comp_options_draw(canvas, panelComp);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 300);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Id"), string_lit("Component identifier.")},
          {string_lit("Name"), string_lit("Component name.")},
          {string_lit("Size"), string_lit("Component size (in bytes).")},
          {string_lit("Align"), string_lit("Component required minimum alignment (in bytes).")},
          {string_lit("Archetypes"), string_lit("Number of archetypes with this component.")},
          {string_lit("Entities"), string_lit("Number of entities with this component.")},
          {string_lit("Total size"), string_lit("Total size taken up by this component.")},
      });

  const u32 numComps = (u32)panelComp->components.size;
  ui_scrollview_begin(canvas, &panelComp->scrollview, ui_table_height(&table, numComps));

  dynarray_for_t(&panelComp->components, DebugEcsCompInfo, compInfo) {
    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, comp_info_bg_color(compInfo));

    ui_label(canvas, fmt_write_scratch("{}", fmt_int(compInfo->id)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, compInfo->name, .selectable = true);
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(compInfo->size)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(compInfo->align)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(compInfo->numArchetypes)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(compInfo->numEntities)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_size(compInfo->numEntities * compInfo->size)));
  }

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
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
      .panel        = ui_panel(ui_vector(800, 500)),
      .scrollview   = ui_scrollview(),
      .nameFilter   = dynstring_create(g_alloc_heap, 32),
      .compSortMode = DebugCompSortMode_Entities,
      .components   = dynarray_create_t(g_alloc_heap, DebugEcsCompInfo, 256));
  return panelEntity;
}
