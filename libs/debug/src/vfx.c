#include "core_alloc.h"
#include "core_diag.h"
#include "core_stringtable.h"
#include "debug_vfx.h"
#include "ecs_world.h"
#include "scene_name.h"
#include "ui.h"
#include "vfx_stats.h"

// clang-format off

static const String g_tooltipFilter = string_static("Filter entries by name.\nSupports glob characters \a.b*\ar and \a.b?\ar (\a.b!\ar prefix to invert).");
static const String g_tooltipFreeze = string_static("Freeze the data set (halts data collection).");

// clang-format on

typedef struct {
  StringHash  nameHash;
  EcsEntityId entity;
  VfxStat     stats[VfxStat_Count];
} DebugVfxInfo;

ecs_comp_define(DebugVfxPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
  bool         freeze;
  DynString    nameFilter;
  DynArray     objects; // DebugVfxInfo[]
};

static void ecs_destruct_vfx_panel(void* data) {
  DebugVfxPanelComp* comp = data;
  dynstring_destroy(&comp->nameFilter);
  dynarray_destroy(&comp->objects);
}

ecs_view_define(VfxObjView) {
  ecs_access_read(SceneNameComp);
  ecs_access_read(VfxStatsComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DebugVfxPanelComp's are exclusively managed here.

  ecs_access_read(DebugPanelComp);
  ecs_access_write(DebugVfxPanelComp);
  ecs_access_write(UiCanvasComp);
}

static bool vfx_panel_filter(DebugVfxPanelComp* panelComp, const String name) {
  if (string_is_empty(panelComp->nameFilter)) {
    return true;
  }
  const String rawFilter = dynstring_view(&panelComp->nameFilter);
  const String filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));
  return string_match_glob(name, filter, StringMatchFlags_IgnoreCase);
}

static String vfx_entity_name(const StringHash nameHash) {
  const String name = stringtable_lookup(g_stringtable, nameHash);
  return string_is_empty(name) ? string_lit("<unnamed>") : name;
}

static void vfx_info_query(DebugVfxPanelComp* panelComp, EcsWorld* world) {
  if (!panelComp->freeze) {
    dynarray_clear(&panelComp->objects);

    EcsView* objView = ecs_world_view_t(world, VfxObjView);
    for (EcsIterator* itr = ecs_view_itr(objView); ecs_view_walk(itr);) {
      const EcsEntityId    entity    = ecs_view_entity(itr);
      const VfxStatsComp*  statsComp = ecs_view_read_t(itr, VfxStatsComp);
      const SceneNameComp* nameComp  = ecs_view_read_t(itr, SceneNameComp);

      if (!vfx_panel_filter(panelComp, vfx_entity_name(nameComp->name))) {
        continue;
      }

      DebugVfxInfo* info = dynarray_push_t(&panelComp->objects, DebugVfxInfo);
      info->entity       = entity;
      info->nameHash     = nameComp->name;
      mem_cpy(mem_var(info->stats), mem_var(statsComp->valuesLast));
    }
  }
}

static void vfx_options_draw(UiCanvasComp* canvas, DebugVfxPanelComp* panelComp) {
  ui_layout_push(canvas);
  ui_style_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 60);
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Filter:"));
  ui_table_next_column(canvas, &table);
  ui_textbox(
      canvas, &panelComp->nameFilter, .placeholder = string_lit("*"), .tooltip = g_tooltipFilter);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Freeze:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &panelComp->freeze, .tooltip = g_tooltipFreeze);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
}

static void vfx_panel_draw(UiCanvasComp* canvas, DebugVfxPanelComp* panelComp) {
  const String title = fmt_write_scratch("{} Vfx Panel", fmt_ui_shape(Diamond));
  ui_panel_begin(
      canvas, &panelComp->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  vfx_options_draw(canvas, panelComp);

  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Fixed, 160);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Name"), string_lit("Entity name.")},
          {string_lit("Entity"), string_lit("Entity identifier.")},
          {string_lit("Particles"), string_lit("Amount of active particles.")},
          {string_lit("Sprites"), string_lit("Amount of sprites being drawn.")},
          {string_lit("Lights"), string_lit("Amount of lights being drawn.")},
          {string_lit("Stamps"), string_lit("Amount of stamps (projected sprites) being drawn.")},
      });

  const u32 numObjects = (u32)panelComp->objects.size;
  ui_scrollview_begin(canvas, &panelComp->scrollview, ui_table_height(&table, numObjects));

  ui_canvas_id_block_next(canvas); // Start the list of objects on its own id block.
  dynarray_for_t(&panelComp->objects, DebugVfxInfo, info) {
    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));

    ui_canvas_id_block_index(canvas, ecs_entity_id_index(info->entity) * 10); // Set a stable id.

    ui_label(canvas, vfx_entity_name(info->nameHash), .selectable = true);
    ui_table_next_column(canvas, &table);
    ui_label_entity(canvas, info->entity);
    for (VfxStat stat = 0; stat != VfxStat_Count; ++stat) {
      ui_table_next_column(canvas, &table);
      ui_label(canvas, fmt_write_scratch("{}", fmt_int(info->stats[stat])));
    }
  }
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugVfxUpdatePanelSys) {
  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId  entity    = ecs_view_entity(itr);
    DebugVfxPanelComp* panelComp = ecs_view_write_t(itr, DebugVfxPanelComp);
    UiCanvasComp*      canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (debug_panel_hidden(ecs_view_read_t(itr, DebugPanelComp)) && !pinned) {
      continue;
    }
    vfx_info_query(panelComp, world);
    vfx_panel_draw(canvas, panelComp);

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_vfx_module) {
  ecs_register_comp(DebugVfxPanelComp, .destructor = ecs_destruct_vfx_panel);

  ecs_register_view(PanelUpdateView);
  ecs_register_view(VfxObjView);

  ecs_register_system(
      DebugVfxUpdatePanelSys, ecs_view_id(PanelUpdateView), ecs_view_id(VfxObjView));
}

EcsEntityId
debug_vfx_panel_open(EcsWorld* world, const EcsEntityId window, const DebugPanelType type) {
  const EcsEntityId  panelEntity = debug_panel_create(world, window, type);
  DebugVfxPanelComp* vfxPanel    = ecs_world_add_t(
      world,
      panelEntity,
      DebugVfxPanelComp,
      .panel      = ui_panel(.size = ui_vector(850, 500)),
      .scrollview = ui_scrollview(),
      .nameFilter = dynstring_create(g_allocHeap, 32),
      .objects    = dynarray_create_t(g_allocHeap, DebugVfxInfo, 128));

  if (type == DebugPanelType_Detached) {
    ui_panel_maximize(&vfxPanel->panel);
  }

  return panelEntity;
}
