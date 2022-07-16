#include "core_alloc.h"
#include "core_array.h"
#include "debug_ecs.h"
#include "debug_register.h"
#include "ecs_runner.h"
#include "ecs_world.h"
#include "ui.h"

// clang-format off

static const String g_tooltipFilter = string_static("Filter entries by name.\nSupports glob characters \a.b*\ar and \a.b?\ar.");
static const String g_tooltipFreeze = string_static("Freeze the data set (halts data collection).");

// clang-format on

typedef struct {
  EcsCompId id;
  String    name;
  u32       size, align;
  u32       numArchetypes, numEntities;
} DebugEcsCompInfo;

typedef struct {
  EcsSystemId  id;
  String       name;
  JobTaskId    taskId;
  JobWorkerId  workerId;
  EcsViewId*   views;
  u32          viewCount;
  TimeDuration duration;
} DebugEcsSysInfo;

typedef enum {
  DebugEcsTab_Components,
  DebugEcsTab_Systems,

  DebugEcsTab_Count,
} DebugEcsTab;

static const String g_ecsTabNames[] = {
    string_static("Components"),
    string_static("Systems"),
};
ASSERT(array_elems(g_ecsTabNames) == DebugEcsTab_Count, "Incorrect number of names");

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

typedef enum {
  DebugSysSortMode_Id,
  DebugSysSortMode_Name,
  DebugSysSortMode_Task,
  DebugSysSortMode_Worker,
  DebugSysSortMode_Duration,

  DebugSysSortMode_Count,
} DebugSysSortMode;

static const String g_sysSortModeNames[] = {
    string_static("Id"),
    string_static("Name"),
    string_static("Task"),
    string_static("Worker"),
    string_static("Duration"),
};
ASSERT(array_elems(g_sysSortModeNames) == DebugSysSortMode_Count, "Incorrect number of names");

ecs_comp_define(DebugEcsPanelComp) {
  UiPanel           panel;
  UiScrollview      scrollview;
  DynString         nameFilter;
  DebugCompSortMode compSortMode;
  DebugSysSortMode  sysSortMode;
  bool              freeze;
  DynArray          components; // DebugEcsCompInfo[]
  DynArray          systems;    // DebugEcsSysInfo[]
};

static void ecs_destruct_ecs_panel(void* data) {
  DebugEcsPanelComp* comp = data;
  dynstring_destroy(&comp->nameFilter);
  dynarray_destroy(&comp->components);
  dynarray_destroy(&comp->systems);
}

static i8 comp_compare_info_name(const void* a, const void* b) {
  return compare_string(field_ptr(a, DebugEcsCompInfo, name), field_ptr(b, DebugEcsCompInfo, name));
}

static i8 comp_compare_info_size(const void* a, const void* b) {
  return compare_u32_reverse(
      field_ptr(a, DebugEcsCompInfo, size), field_ptr(b, DebugEcsCompInfo, size));
}

static i8 comp_compare_info_archetypes(const void* a, const void* b) {
  return compare_u32_reverse(
      field_ptr(a, DebugEcsCompInfo, numArchetypes), field_ptr(b, DebugEcsCompInfo, numArchetypes));
}

static i8 comp_compare_info_entities(const void* a, const void* b) {
  return compare_u32_reverse(
      field_ptr(a, DebugEcsCompInfo, numEntities), field_ptr(b, DebugEcsCompInfo, numEntities));
}

static i8 sys_compare_info_id(const void* a, const void* b) {
  return compare_u32(field_ptr(a, DebugEcsSysInfo, id), field_ptr(b, DebugEcsSysInfo, id));
}

static i8 sys_compare_info_name(const void* a, const void* b) {
  return compare_string(field_ptr(a, DebugEcsSysInfo, name), field_ptr(b, DebugEcsSysInfo, name));
}

static i8 sys_compare_info_task(const void* a, const void* b) {
  return compare_u32(field_ptr(a, DebugEcsSysInfo, taskId), field_ptr(b, DebugEcsSysInfo, taskId));
}

static i8 sys_compare_info_worker(const void* a, const void* b) {
  const DebugEcsSysInfo* sysA        = a;
  const DebugEcsSysInfo* sysB        = b;
  i8                     workerOrder = compare_u16(&sysA->workerId, &sysB->workerId);
  if (!workerOrder) {
    workerOrder = compare_u32(&sysA->taskId, &sysB->taskId);
  }
  return workerOrder;
}

static i8 sys_compare_info_duration(const void* a, const void* b) {
  return compare_u64_reverse(
      field_ptr(a, DebugEcsSysInfo, duration), field_ptr(b, DebugEcsSysInfo, duration));
}

static bool ecs_panel_filter(DebugEcsPanelComp* panelComp, const String name) {
  if (string_is_empty(panelComp->nameFilter)) {
    return true;
  }
  const String rawFilter = dynstring_view(&panelComp->nameFilter);
  const String filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));
  return string_match_glob(name, filter, StringMatchFlags_IgnoreCase);
}

static void comp_info_query(DebugEcsPanelComp* panelComp, EcsWorld* world) {
  dynarray_clear(&panelComp->components);

  const EcsDef* def = ecs_world_def(world);
  for (EcsCompId id = 0; id != ecs_def_comp_count(def); ++id) {
    if (!ecs_panel_filter(panelComp, ecs_def_comp_name(def, id))) {
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
    dynarray_sort(&panelComp->components, comp_compare_info_name);
    break;
  case DebugCompSortMode_Size:
    dynarray_sort(&panelComp->components, comp_compare_info_size);
    break;
  case DebugCompSortMode_Archetypes:
    dynarray_sort(&panelComp->components, comp_compare_info_archetypes);
    break;
  case DebugCompSortMode_Entities:
    dynarray_sort(&panelComp->components, comp_compare_info_entities);
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
  ui_table_add_column(&table, UiTableColumn_Fixed, 60);
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Filter:"));
  ui_table_next_column(canvas, &table);
  ui_textbox(
      canvas, &panelComp->nameFilter, .placeholder = string_lit("*"), .tooltip = g_tooltipFilter);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Sort:"));
  ui_table_next_column(canvas, &table);
  ui_select(canvas, (i32*)&panelComp->compSortMode, g_compSortModeNames, DebugCompSortMode_Count);

  ui_layout_pop(canvas);
}

static void comp_panel_tab_draw(UiCanvasComp* canvas, DebugEcsPanelComp* panelComp) {
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

  ui_canvas_id_block_next(canvas); // Start the list of components on its own id block.
  dynarray_for_t(&panelComp->components, DebugEcsCompInfo, compInfo) {
    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, comp_info_bg_color(compInfo));

    ui_canvas_id_block_index(canvas, compInfo->id * 10); // Set a stable id based on the comp id.

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
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void sys_info_query(DebugEcsPanelComp* panelComp, EcsWorld* world) {
  if (!panelComp->freeze) {
    dynarray_clear(&panelComp->systems);
    const EcsWorldStats stats = ecs_world_stats_query(world);
    const EcsDef*       def   = ecs_world_def(world);
    for (EcsSystemId id = 0; id != ecs_def_system_count(def); ++id) {
      if (!ecs_panel_filter(panelComp, ecs_def_system_name(def, id))) {
        continue;
      }
      *dynarray_push_t(&panelComp->systems, DebugEcsSysInfo) = (DebugEcsSysInfo){
          .id        = id,
          .name      = ecs_def_system_name(def, id),
          .taskId    = ecs_runner_graph_task(g_ecsRunningRunner, id),
          .workerId  = stats.sysStats[id].workerId,
          .views     = ecs_def_system_views(def, id).values,
          .viewCount = (u32)ecs_def_system_views(def, id).count,
          .duration  = stats.sysStats[id].avgDur,
      };
    }
  }

  switch (panelComp->sysSortMode) {
  case DebugSysSortMode_Id:
    dynarray_sort(&panelComp->systems, sys_compare_info_id);
    break;
  case DebugSysSortMode_Name:
    dynarray_sort(&panelComp->systems, sys_compare_info_name);
    break;
  case DebugSysSortMode_Task:
    dynarray_sort(&panelComp->systems, sys_compare_info_task);
    break;
  case DebugSysSortMode_Worker:
    dynarray_sort(&panelComp->systems, sys_compare_info_worker);
    break;
  case DebugSysSortMode_Duration:
    dynarray_sort(&panelComp->systems, sys_compare_info_duration);
    break;
  case DebugSysSortMode_Count:
    break;
  }
}

static UiColor sys_info_bg_color(const DebugEcsSysInfo* compInfo) {
  if (compInfo->duration >= time_millisecond) {
    return ui_color(64, 16, 16, 192);
  }
  if (compInfo->duration >= time_microseconds(500)) {
    return ui_color(78, 78, 16, 192);
  }
  return ui_color(48, 48, 48, 192);
}

static void sys_options_draw(UiCanvasComp* canvas, DebugEcsPanelComp* panelComp) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 60);
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Filter:"));
  ui_table_next_column(canvas, &table);
  ui_textbox(
      canvas, &panelComp->nameFilter, .placeholder = string_lit("*"), .tooltip = g_tooltipFilter);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Sort:"));
  ui_table_next_column(canvas, &table);
  ui_select(canvas, (i32*)&panelComp->sysSortMode, g_sysSortModeNames, DebugSysSortMode_Count);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Freeze:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &panelComp->freeze, .tooltip = g_tooltipFreeze);

  ui_layout_pop(canvas);
}

static String sys_views_tooltip_scratch(const EcsDef* ecsDef, const DebugEcsSysInfo* sysInfo) {
  DynString str = dynstring_create_over(alloc_alloc(g_alloc_scratch, 2 * usize_kibibyte, 1));
  dynstring_append(&str, string_lit("Views:\n"));
  for (u32 i = 0; i != sysInfo->viewCount; ++i) {
    const EcsViewId viewId = sysInfo->views[i];
    fmt_write(&str, "  [{}] {}\n", fmt_int(viewId), fmt_text(ecs_def_view_name(ecsDef, viewId)));
  }
  return dynstring_view(&str);
}

static void
sys_panel_tab_draw(UiCanvasComp* canvas, DebugEcsPanelComp* panelComp, const EcsDef* ecsDef) {
  sys_options_draw(canvas, panelComp);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 325);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Id"), string_lit("System identifier.")},
          {string_lit("Name"), string_lit("System name.")},
          {string_lit("Task"), string_lit("Identifier of the job-graph task of this system.")},
          {string_lit("Worker"), string_lit("Identifier job-worker that ran this system last.")},
          {string_lit("Views"), string_lit("Amount of views the system accesses.")},
          {string_lit("Duration"), string_lit("Last execution duration of this system.")},
      });

  const u32 numSystems = (u32)panelComp->systems.size;
  ui_scrollview_begin(canvas, &panelComp->scrollview, ui_table_height(&table, numSystems));

  ui_canvas_id_block_next(canvas); // Start the list of systems on its own id block.
  dynarray_for_t(&panelComp->systems, DebugEcsSysInfo, sysInfo) {
    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, sys_info_bg_color(sysInfo));

    ui_canvas_id_block_index(canvas, sysInfo->id * 10); // Set a stable id based on the comp id.

    ui_label(canvas, fmt_write_scratch("{}", fmt_int(sysInfo->id)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, sysInfo->name, .selectable = true);
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(sysInfo->taskId)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(sysInfo->workerId)));
    ui_table_next_column(canvas, &table);
    ui_label(
        canvas,
        fmt_write_scratch("{}", fmt_int(sysInfo->viewCount)),
        .tooltip = sys_views_tooltip_scratch(ecsDef, sysInfo));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_duration(sysInfo->duration)));
  }
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void ecs_panel_draw(UiCanvasComp* canvas, DebugEcsPanelComp* panelComp, EcsWorld* world) {
  const String title = fmt_write_scratch("{} Ecs Panel", fmt_ui_shape(Extension));
  ui_panel_begin(
      canvas,
      &panelComp->panel,
      .title    = title,
      .tabNames = g_ecsTabNames,
      .tabCount = DebugEcsTab_Count);

  switch (panelComp->panel.activeTab) {
  case DebugEcsTab_Components:
    comp_info_query(panelComp, world);
    comp_panel_tab_draw(canvas, panelComp);
    break;
  case DebugEcsTab_Systems:
    sys_info_query(panelComp, world);
    sys_panel_tab_draw(canvas, panelComp, ecs_world_def(world));
    break;
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugEcsPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DebugEcsUpdatePanelSys) {
  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId  entity    = ecs_view_entity(itr);
    DebugEcsPanelComp* panelComp = ecs_view_write_t(itr, DebugEcsPanelComp);
    UiCanvasComp*      canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    ecs_panel_draw(canvas, panelComp, world);

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

  ecs_register_system(DebugEcsUpdatePanelSys, ecs_view_id(PanelUpdateView));
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
      .sysSortMode  = DebugSysSortMode_Task,
      .components   = dynarray_create_t(g_alloc_heap, DebugEcsCompInfo, 256),
      .systems      = dynarray_create_t(g_alloc_heap, DebugEcsSysInfo, 256));
  return panelEntity;
}
