#include "core_alloc.h"
#include "core_array.h"
#include "core_format.h"
#include "core_path.h"
#include "dev_ecs.h"
#include "dev_panel.h"
#include "ecs_def.h"
#include "ecs_module.h"
#include "ecs_runner.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "jobs_dot.h"
#include "jobs_executor.h"
#include "log_logger.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_scrollview.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_table.h"
#include "ui_widget.h"

// clang-format off

static const String g_tooltipFilter    = string_static("Filter entries by name.\nSupports glob characters \a.b*\ar and \a.b?\ar (\a.b!\ar prefix to invert).");
static const String g_tooltipFreeze    = string_static("Freeze the data set (halts data collection).");
static const String g_tooltipDumpGraph = string_static("Dump the current task graph as a dot file.");

// clang-format on

typedef struct {
  EcsCompId id;
  String    name;
  u32       size, align;
  u32       numArchetypes, numEntities;
} DevEcsCompInfo;

typedef struct {
  EcsViewId id;
  String    name;
  String    moduleName;
  u32       entityCount, chunkCount;
} DevEcsViewInfo;

typedef struct {
  EcsArchetypeId id;
  u32            entityCount, chunkCount, entitiesPerChunk;
  usize          size;
  BitSet         compMask;
  u32            compCount;
} DevEcsArchetypeInfo;

typedef struct {
  String         name;
  EcsViewId*     views;
  EcsSystemId    id;
  u32            viewCount;
  i32            definedOrder; // Configured ordering constraint.
  u16            parallelCount;
  EcsSystemFlags flags : 16;
  TimeDuration   duration;
} DevEcsSysInfo;

typedef enum {
  DevEcsTab_Components,
  DevEcsTab_Views,
  DevEcsTab_Archetypes,
  DevEcsTab_Systems,

  DevEcsTab_Count,
} DevEcsTab;

static const String g_ecsTabNames[] = {
    string_static("Components"),
    string_static("Views"),
    string_static("Archetypes"),
    string_static("Systems"),
};
ASSERT(array_elems(g_ecsTabNames) == DevEcsTab_Count, "Incorrect number of names");

typedef enum {
  DevCompSortMode_Id,
  DevCompSortMode_Name,
  DevCompSortMode_Size,
  DevCompSortMode_SizeTotal,
  DevCompSortMode_Archetypes,
  DevCompSortMode_Entities,

  DevCompSortMode_Count,
} DevCompSortMode;

static const String g_compSortModeNames[] = {
    string_static("Id"),
    string_static("Name"),
    string_static("Size"),
    string_static("SizeTotal"),
    string_static("Archetypes"),
    string_static("Entities"),
};
ASSERT(array_elems(g_compSortModeNames) == DevCompSortMode_Count, "Incorrect number of names");

typedef enum {
  DevArchSortMode_Id,
  DevArchSortMode_ComponentCount,
  DevArchSortMode_EntityCount,
  DevArchSortMode_ChunkCount,

  DevArchSortMode_Count,
} DevArchSortMode;

static const String g_archSortModeNames[] = {
    string_static("Id"),
    string_static("Components"),
    string_static("Entities"),
    string_static("Chunks"),
};
ASSERT(array_elems(g_archSortModeNames) == DevArchSortMode_Count, "Incorrect number of names");

typedef enum {
  DevSysSortMode_Id,
  DevSysSortMode_Name,
  DevSysSortMode_Duration,
  DevSysSortMode_Order,

  DevSysSortMode_Count,
} DevSysSortMode;

static const String g_sysSortModeNames[] = {
    string_static("Id"),
    string_static("Name"),
    string_static("Duration"),
    string_static("Order"),
};
ASSERT(array_elems(g_sysSortModeNames) == DevSysSortMode_Count, "Incorrect number of names");

ecs_comp_define(DevEcsPanelComp) {
  UiPanel         panel;
  UiScrollview    scrollview;
  DynString       nameFilter;
  DevCompSortMode compSortMode;
  DevArchSortMode archSortMode;
  DevSysSortMode  sysSortMode;
  bool            freeze, hideEmptyArchetypes;
  DynArray        components; // DevEcsCompInfo[]
  DynArray        views;      // DevEcsViewInfo[]
  DynArray        archetypes; // DevEcsArchetypeInfo[]
  DynArray        systems;    // DevEcsSysInfo[]
};

static void ecs_destruct_ecs_panel(void* data) {
  DevEcsPanelComp* comp = data;
  dynstring_destroy(&comp->nameFilter);
  dynarray_destroy(&comp->components);
  dynarray_destroy(&comp->views);
  dynarray_destroy(&comp->archetypes);
  dynarray_destroy(&comp->systems);
}

static i8 comp_compare_info_name(const void* a, const void* b) {
  return compare_string(field_ptr(a, DevEcsCompInfo, name), field_ptr(b, DevEcsCompInfo, name));
}

static i8 comp_compare_info_size(const void* a, const void* b) {
  return compare_u32_reverse(
      field_ptr(a, DevEcsCompInfo, size), field_ptr(b, DevEcsCompInfo, size));
}

static i8 comp_compare_info_size_total(const void* a, const void* b) {
  const DevEcsCompInfo* aInfo = a;
  const DevEcsCompInfo* bInfo = b;

  const usize totalA = aInfo->numEntities * aInfo->size;
  const usize totalB = bInfo->numEntities * bInfo->size;
  return compare_u32_reverse(&totalA, &totalB);
}

static i8 comp_compare_info_archetypes(const void* a, const void* b) {
  return compare_u32_reverse(
      field_ptr(a, DevEcsCompInfo, numArchetypes), field_ptr(b, DevEcsCompInfo, numArchetypes));
}

static i8 comp_compare_info_entities(const void* a, const void* b) {
  return compare_u32_reverse(
      field_ptr(a, DevEcsCompInfo, numEntities), field_ptr(b, DevEcsCompInfo, numEntities));
}

static i8 arch_compare_info_components(const void* a, const void* b) {
  const DevEcsArchetypeInfo* archA = a;
  const DevEcsArchetypeInfo* archB = b;
  const i8                   c     = compare_u32_reverse(&archA->compCount, &archB->compCount);
  return c ? c : compare_u32(&archA->id, &archB->id);
}

static i8 arch_compare_info_entities(const void* a, const void* b) {
  const DevEcsArchetypeInfo* archA = a;
  const DevEcsArchetypeInfo* archB = b;
  const i8                   c     = compare_u32_reverse(&archA->entityCount, &archB->entityCount);
  return c ? c : compare_u32(&archA->id, &archB->id);
}

static i8 arch_compare_info_chunks(const void* a, const void* b) {
  const DevEcsArchetypeInfo* archA = a;
  const DevEcsArchetypeInfo* archB = b;
  const i8                   c     = compare_u32_reverse(&archA->chunkCount, &archB->chunkCount);
  return c ? c : compare_u32(&archA->id, &archB->id);
}

static i8 sys_compare_info_id(const void* a, const void* b) {
  return compare_u32(field_ptr(a, DevEcsSysInfo, id), field_ptr(b, DevEcsSysInfo, id));
}

static i8 sys_compare_info_name(const void* a, const void* b) {
  return compare_string(field_ptr(a, DevEcsSysInfo, name), field_ptr(b, DevEcsSysInfo, name));
}

static i8 sys_compare_info_duration(const void* a, const void* b) {
  const DevEcsSysInfo* infoA = a;
  const DevEcsSysInfo* infoB = b;
  const i8             comp  = compare_u64_reverse(&infoA->duration, &infoB->duration);
  return comp ? comp : compare_u32(&infoA->id, &infoB->id);
}

static i8 sys_compare_info_order(const void* a, const void* b) {
  const DevEcsSysInfo* infoA = a;
  const DevEcsSysInfo* infoB = b;
  const i8             comp  = compare_i32(&infoA->definedOrder, &infoB->definedOrder);
  return comp ? comp : compare_u32(&infoA->id, &infoB->id);
}

static void ecs_dump_graph(const JobGraph* graph) {
  const String pathScratch = path_build_scratch(
      path_parent(g_pathExecutable),
      string_lit("logs"),
      path_name_timestamp_scratch(path_stem(g_pathExecutable), string_lit("dot")));

  const FileResult res = jobs_dot_dump_graph_to_path(pathScratch, graph);
  if (res == FileResult_Success) {
    log_i("Dumped ecs graph", log_param("path", fmt_path(pathScratch)));
  } else {
    log_e(
        "Failed to dump ecs graph",
        log_param("error", fmt_text(file_result_str(res))),
        log_param("path", fmt_path(pathScratch)));
  }
}

static bool ecs_panel_filter(DevEcsPanelComp* panelComp, const String name) {
  if (string_is_empty(panelComp->nameFilter)) {
    return true;
  }
  const String rawFilter = dynstring_view(&panelComp->nameFilter);
  const String filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));
  return string_match_glob(name, filter, StringMatchFlags_IgnoreCase);
}

static void comp_info_query(DevEcsPanelComp* panelComp, EcsWorld* world) {
  dynarray_clear(&panelComp->components);

  const EcsDef* def = ecs_world_def(world);
  for (EcsCompId id = 0; id != ecs_def_comp_count(def); ++id) {
    if (!ecs_panel_filter(panelComp, ecs_def_comp_name(def, id))) {
      continue;
    }

    *dynarray_push_t(&panelComp->components, DevEcsCompInfo) = (DevEcsCompInfo){
        .id            = id,
        .name          = ecs_def_comp_name(def, id),
        .size          = (u32)ecs_def_comp_size(def, id),
        .align         = (u32)ecs_def_comp_align(def, id),
        .numArchetypes = ecs_world_archetype_count_with_comp(world, id),
        .numEntities   = ecs_world_entity_count_with_comp(world, id),
    };
  }

  switch (panelComp->compSortMode) {
  case DevCompSortMode_Name:
    dynarray_sort(&panelComp->components, comp_compare_info_name);
    break;
  case DevCompSortMode_Size:
    dynarray_sort(&panelComp->components, comp_compare_info_size);
    break;
  case DevCompSortMode_SizeTotal:
    dynarray_sort(&panelComp->components, comp_compare_info_size_total);
    break;
  case DevCompSortMode_Archetypes:
    dynarray_sort(&panelComp->components, comp_compare_info_archetypes);
    break;
  case DevCompSortMode_Entities:
    dynarray_sort(&panelComp->components, comp_compare_info_entities);
    break;
  case DevCompSortMode_Id:
  case DevCompSortMode_Count:
    break;
  }
}

static UiColor comp_info_bg_color(const DevEcsCompInfo* compInfo) {
  return compInfo->numEntities ? ui_color(16, 64, 16, 192) : ui_color(48, 48, 48, 192);
}

static void comp_options_draw(UiCanvasComp* canvas, DevEcsPanelComp* panelComp) {
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
  ui_select(canvas, (i32*)&panelComp->compSortMode, g_compSortModeNames, DevCompSortMode_Count);

  ui_layout_pop(canvas);
}

static void comp_panel_tab_draw(UiCanvasComp* canvas, DevEcsPanelComp* panelComp) {
  comp_options_draw(canvas, panelComp);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None, UiLayer_Normal);

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
  const f32 height   = ui_table_height(&table, numComps);
  ui_scrollview_begin(canvas, &panelComp->scrollview, UiLayer_Normal, height);

  ui_canvas_id_block_next(canvas); // Start the list of components on its own id block.
  for (u32 compIdx = 0; compIdx != numComps; ++compIdx) {
    const DevEcsCompInfo* compInfo = dynarray_at_t(&panelComp->components, compIdx, DevEcsCompInfo);
    const f32             y        = ui_table_height(&table, compIdx);
    const UiScrollviewCull cull    = ui_scrollview_cull(&panelComp->scrollview, y, table.rowHeight);
    if (cull == UiScrollviewCull_After) {
      break;
    }
    if (cull == UiScrollviewCull_Before) {
      continue;
    }

    ui_table_jump_row(canvas, &table, compIdx);
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

static void view_info_query(DevEcsPanelComp* panelComp, EcsWorld* world) {
  if (!panelComp->freeze) {
    dynarray_clear(&panelComp->views);

    const EcsDef* def = ecs_world_def(world);
    for (EcsViewId id = 0; id != ecs_def_view_count(def); ++id) {
      if (!ecs_panel_filter(panelComp, ecs_def_view_name(def, id))) {
        continue;
      }

      *dynarray_push_t(&panelComp->views, DevEcsViewInfo) = (DevEcsViewInfo){
          .id          = id,
          .name        = ecs_def_view_name(def, id),
          .moduleName  = ecs_def_module_name(def, ecs_def_view_module(def, id)),
          .entityCount = ecs_world_view_entities(world, id),
          .chunkCount  = ecs_world_view_chunks(world, id),
      };
    }
  }
}

static void view_options_draw(UiCanvasComp* canvas, DevEcsPanelComp* panelComp) {
  ui_layout_push(canvas);

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

  ui_layout_pop(canvas);
}

static void view_panel_tab_draw(UiCanvasComp* canvas, DevEcsPanelComp* panelComp) {
  view_options_draw(canvas, panelComp);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None, UiLayer_Normal);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Id"), string_lit("View identifier.")},
          {string_lit("Name"), string_lit("View name.")},
          {string_lit("Module"), string_lit("Name of the module that this view belongs to.")},
          {string_lit("Entities"), string_lit("Amount of entities in this view.")},
          {string_lit("Chunks"), string_lit("Amount of archetype chunks in this view.")},
      });

  const u32 numViews = (u32)panelComp->views.size;
  const f32 height   = ui_table_height(&table, numViews);
  ui_scrollview_begin(canvas, &panelComp->scrollview, UiLayer_Normal, height);

  ui_canvas_id_block_next(canvas); // Start the list of views on its own id block.
  for (u32 viewIdx = 0; viewIdx != numViews; ++viewIdx) {
    const DevEcsViewInfo*  viewInfo = dynarray_at_t(&panelComp->views, viewIdx, DevEcsViewInfo);
    const f32              y        = ui_table_height(&table, viewIdx);
    const UiScrollviewCull cull = ui_scrollview_cull(&panelComp->scrollview, y, table.rowHeight);
    if (cull == UiScrollviewCull_After) {
      break;
    }
    if (cull == UiScrollviewCull_Before) {
      continue;
    }

    ui_table_jump_row(canvas, &table, viewIdx);
    ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));
    ui_canvas_id_block_index(canvas, viewInfo->id * 10); // Set a stable id based on the view id.

    ui_label(canvas, fmt_write_scratch("{}", fmt_int(viewInfo->id)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, viewInfo->name);
    ui_table_next_column(canvas, &table);
    ui_label(canvas, viewInfo->moduleName);
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(viewInfo->entityCount)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(viewInfo->chunkCount)));
  }
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void arch_info_query(DevEcsPanelComp* panelComp, EcsWorld* world) {
  if (!panelComp->freeze) {
    dynarray_clear(&panelComp->archetypes);
    for (EcsArchetypeId id = 0; id != ecs_world_archetype_count(world); ++id) {
      if (panelComp->hideEmptyArchetypes && !ecs_world_archetype_entities(world, id)) {
        continue;
      }
      const BitSet compMask = ecs_world_component_mask(world, id);
      *dynarray_push_t(&panelComp->archetypes, DevEcsArchetypeInfo) = (DevEcsArchetypeInfo){
          .id               = id,
          .entityCount      = ecs_world_archetype_entities(world, id),
          .chunkCount       = ecs_world_archetype_chunks(world, id),
          .entitiesPerChunk = ecs_world_archetype_entities_per_chunk(world, id),
          .size             = ecs_world_archetype_size(world, id),
          .compMask         = compMask,
          .compCount        = (u32)bitset_count(compMask),
      };
    }
  }

  switch (panelComp->archSortMode) {
  case DevArchSortMode_ComponentCount:
    dynarray_sort(&panelComp->archetypes, arch_compare_info_components);
    break;
  case DevArchSortMode_EntityCount:
    dynarray_sort(&panelComp->archetypes, arch_compare_info_entities);
    break;
  case DevArchSortMode_ChunkCount:
    dynarray_sort(&panelComp->archetypes, arch_compare_info_chunks);
    break;
  case DevArchSortMode_Id:
  case DevArchSortMode_Count:
    break;
  }
}

static void arch_options_draw(UiCanvasComp* canvas, DevEcsPanelComp* panelComp) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 110);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Sort:"));
  ui_table_next_column(canvas, &table);
  ui_select(canvas, (i32*)&panelComp->archSortMode, g_archSortModeNames, DevArchSortMode_Count);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Freeze:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &panelComp->freeze, .tooltip = g_tooltipFreeze);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Hide empty:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &panelComp->hideEmptyArchetypes);

  ui_layout_pop(canvas);
}

static String arch_comp_mask_tooltip_scratch(const EcsDef* ecsDef, const BitSet compMask) {
  DynString str = dynstring_create_over(alloc_alloc(g_allocScratch, 2 * usize_kibibyte, 1));
  dynstring_append(&str, string_lit("Components:\n"));
  bitset_for(compMask, compId) {
    const String compName = ecs_def_comp_name(ecsDef, (EcsCompId)compId);
    const usize  compSize = ecs_def_comp_size(ecsDef, (EcsCompId)compId);
    fmt_write(&str, "- {} ({})\n", fmt_text(compName), fmt_size(compSize));
  }
  return dynstring_view(&str);
}

static void
arch_panel_tab_draw(UiCanvasComp* canvas, DevEcsPanelComp* panelComp, const EcsDef* ecsDef) {
  arch_options_draw(canvas, panelComp);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None, UiLayer_Normal);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 125);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Id"), string_lit("Archetype identifier.")},
          {string_lit("Components"), string_lit("Archetype components.")},
          {string_lit("Entities"), string_lit("Amount of entities in this archetype.")},
          {string_lit("Chunks"), string_lit("Amount of chunks in this archetype.")},
          {string_lit("Size"), string_lit("Total size of this archetype.")},
          {string_lit("Entities per chunk"), string_lit("Amount of entities per chunk.")},
      });

  const u32 numArchetypes = (u32)panelComp->archetypes.size;
  const f32 height        = ui_table_height(&table, numArchetypes);
  ui_scrollview_begin(canvas, &panelComp->scrollview, UiLayer_Normal, height);

  ui_canvas_id_block_next(canvas); // Start the list of archetypes on its own id block.
  for (u32 archIdx = 0; archIdx != numArchetypes; ++archIdx) {
    const DevEcsArchetypeInfo* archInfo =
        dynarray_at_t(&panelComp->archetypes, archIdx, DevEcsArchetypeInfo);
    const f32              y    = ui_table_height(&table, archIdx);
    const UiScrollviewCull cull = ui_scrollview_cull(&panelComp->scrollview, y, table.rowHeight);
    if (cull == UiScrollviewCull_After) {
      break;
    }
    if (cull == UiScrollviewCull_Before) {
      continue;
    }

    ui_table_jump_row(canvas, &table, archIdx);
    ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));
    ui_canvas_id_block_index(canvas, archInfo->id * 10); // Set a stable id based on the arch id.

    ui_label(canvas, fmt_write_scratch("{}", fmt_int(archInfo->id)));
    ui_table_next_column(canvas, &table);
    ui_label(
        canvas,
        fmt_write_scratch("{}", fmt_int(archInfo->compCount)),
        .tooltip        = arch_comp_mask_tooltip_scratch(ecsDef, archInfo->compMask),
        .tooltipMaxSize = {500, 1000});
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(archInfo->entityCount)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(archInfo->chunkCount)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_size(archInfo->size)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(archInfo->entitiesPerChunk)));
  }
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void sys_info_query(DevEcsPanelComp* panelComp, EcsWorld* world) {
  if (!panelComp->freeze) {
    dynarray_clear(&panelComp->systems);

    const EcsRunner* runner = g_ecsRunningRunner;
    const EcsDef*    def    = ecs_world_def(world);

    for (EcsSystemId id = 0; id != ecs_def_system_count(def); ++id) {
      if (!ecs_panel_filter(panelComp, ecs_def_system_name(def, id))) {
        continue;
      }
      *dynarray_push_t(&panelComp->systems, DevEcsSysInfo) = (DevEcsSysInfo){
          .id            = id,
          .name          = ecs_def_system_name(def, id),
          .definedOrder  = ecs_def_system_order(def, id),
          .views         = ecs_def_system_views(def, id).values,
          .viewCount     = (u32)ecs_def_system_views(def, id).count,
          .parallelCount = ecs_def_system_parallel(def, id),
          .flags         = ecs_def_system_flags(def, id),
          .duration      = ecs_runner_duration_avg(runner, id),
      };
    }
  }

  switch (panelComp->sysSortMode) {
  case DevSysSortMode_Id:
    dynarray_sort(&panelComp->systems, sys_compare_info_id);
    break;
  case DevSysSortMode_Name:
    dynarray_sort(&panelComp->systems, sys_compare_info_name);
    break;
  case DevSysSortMode_Duration:
    dynarray_sort(&panelComp->systems, sys_compare_info_duration);
    break;
  case DevSysSortMode_Order:
    dynarray_sort(&panelComp->systems, sys_compare_info_order);
    break;
  case DevSysSortMode_Count:
    break;
  }
}

static UiColor sys_info_bg_color(const DevEcsSysInfo* compInfo) {
  if (compInfo->duration >= time_millisecond) {
    return ui_color(64, 16, 16, 192);
  }
  if (compInfo->duration >= time_microseconds(500)) {
    return ui_color(78, 78, 16, 192);
  }
  return ui_color(48, 48, 48, 192);
}

static void sys_options_draw(UiCanvasComp* canvas, DevEcsPanelComp* panelComp) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 60);
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 120);
  ui_table_add_column(&table, UiTableColumn_Fixed, 70);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Filter:"));
  ui_table_next_column(canvas, &table);
  ui_textbox(
      canvas, &panelComp->nameFilter, .placeholder = string_lit("*"), .tooltip = g_tooltipFilter);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Sort:"));
  ui_table_next_column(canvas, &table);
  ui_select(canvas, (i32*)&panelComp->sysSortMode, g_sysSortModeNames, DevSysSortMode_Count);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Freeze:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &panelComp->freeze, .tooltip = g_tooltipFreeze);
  ui_table_next_column(canvas, &table);
  if (ui_button(canvas, .label = string_lit("Dump graph"), .tooltip = g_tooltipDumpGraph)) {
    const JobGraph* currentGraph = ecs_runner_graph(g_ecsRunningRunner);
    ecs_dump_graph(currentGraph);
  }

  ui_layout_pop(canvas);
}

static String sys_views_tooltip_scratch(const EcsDef* ecsDef, const DevEcsSysInfo* sysInfo) {
  DynString str = dynstring_create_over(alloc_alloc(g_allocScratch, 2 * usize_kibibyte, 1));
  dynstring_append(&str, string_lit("Views:\n"));
  for (u32 i = 0; i != sysInfo->viewCount; ++i) {
    const EcsViewId viewId = sysInfo->views[i];
    fmt_write(&str, "  [{}] {}\n", fmt_int(viewId), fmt_text(ecs_def_view_name(ecsDef, viewId)));
  }
  return dynstring_view(&str);
}

static UiColor sys_defined_order_color(const DevEcsSysInfo* sysInfo) {
  if (sysInfo->flags & EcsSystemFlags_ThreadAffinity) {
    return ui_color_teal;
  }
  if (sysInfo->flags & EcsSystemFlags_Exclusive) {
    return ui_color_orange;
  }
  return ui_color_white;
}

static void
sys_panel_tab_draw(UiCanvasComp* canvas, DevEcsPanelComp* panelComp, const EcsDef* ecsDef) {
  sys_options_draw(canvas, panelComp);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None, UiLayer_Normal);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 325);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Id"), string_lit("System identifier.")},
          {string_lit("Name"), string_lit("System name.")},
          {string_lit("Order"), string_lit("Defined system order.")},
          {string_lit("Views"), string_lit("Amount of views the system accesses.")},
          {string_lit("Parallel"), string_lit("Amount of parallel tasks.")},
          {string_lit("Duration"), string_lit("Last execution duration of this system.")},
      });

  const u32 numSystems = (u32)panelComp->systems.size;
  const f32 height     = ui_table_height(&table, numSystems);
  ui_scrollview_begin(canvas, &panelComp->scrollview, UiLayer_Normal, height);

  const bool hasMultipleWorkers = g_jobsWorkerCount > 1;

  ui_canvas_id_block_next(canvas); // Start the list of systems on its own id block.
  for (u32 sysIdx = 0; sysIdx != numSystems; ++sysIdx) {
    const DevEcsSysInfo*   sysInfo = dynarray_at_t(&panelComp->systems, sysIdx, DevEcsSysInfo);
    const f32              y       = ui_table_height(&table, sysIdx);
    const UiScrollviewCull cull    = ui_scrollview_cull(&panelComp->scrollview, y, table.rowHeight);
    if (cull == UiScrollviewCull_After) {
      break;
    }
    if (cull == UiScrollviewCull_Before) {
      continue;
    }

    ui_table_jump_row(canvas, &table, sysIdx);
    ui_table_draw_row_bg(canvas, &table, sys_info_bg_color(sysInfo));
    ui_canvas_id_block_index(canvas, sysInfo->id * 10); // Set a stable id based on the comp id.

    ui_label(canvas, fmt_write_scratch("{}", fmt_int(sysInfo->id)));

    ui_table_next_column(canvas, &table);
    ui_label(canvas, sysInfo->name, .selectable = true);

    ui_table_next_column(canvas, &table);
    ui_style_push(canvas);
    ui_style_color(canvas, sys_defined_order_color(sysInfo));
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(sysInfo->definedOrder)));
    ui_style_pop(canvas);

    ui_table_next_column(canvas, &table);
    ui_label(
        canvas,
        fmt_write_scratch("{}", fmt_int(sysInfo->viewCount)),
        .tooltip = sys_views_tooltip_scratch(ecsDef, sysInfo));

    ui_table_next_column(canvas, &table);
    if (hasMultipleWorkers) {
      ui_label(canvas, fmt_write_scratch("{}", fmt_int(sysInfo->parallelCount)));
    } else {
      ui_label(canvas, string_lit("N/A"));
    }

    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_duration(sysInfo->duration)));
  }
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void ecs_panel_draw(UiCanvasComp* canvas, DevEcsPanelComp* panelComp, EcsWorld* world) {
  const String title = fmt_write_scratch("{} Ecs Panel", fmt_ui_shape(Extension));
  ui_panel_begin(
      canvas,
      &panelComp->panel,
      .title       = title,
      .tabNames    = g_ecsTabNames,
      .tabCount    = DevEcsTab_Count,
      .topBarColor = ui_color(100, 0, 0, 192));

  switch (panelComp->panel.activeTab) {
  case DevEcsTab_Components:
    comp_info_query(panelComp, world);
    comp_panel_tab_draw(canvas, panelComp);
    break;
  case DevEcsTab_Views:
    view_info_query(panelComp, world);
    view_panel_tab_draw(canvas, panelComp);
    break;
  case DevEcsTab_Archetypes:
    arch_info_query(panelComp, world);
    arch_panel_tab_draw(canvas, panelComp, ecs_world_def(world));
    break;
  case DevEcsTab_Systems:
    sys_info_query(panelComp, world);
    sys_panel_tab_draw(canvas, panelComp, ecs_world_def(world));
    break;
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevEcsPanelComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DevEcsPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DevEcsUpdatePanelSys) {
  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId entity    = ecs_view_entity(itr);
    DevEcsPanelComp*  panelComp = ecs_view_write_t(itr, DevEcsPanelComp);
    UiCanvasComp*     canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp)) && !pinned) {
      continue;
    }
    ecs_panel_draw(canvas, panelComp, world);

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(dev_ecs_module) {
  ecs_register_comp(DevEcsPanelComp, .destructor = ecs_destruct_ecs_panel);

  ecs_register_view(PanelUpdateView);

  ecs_register_system(DevEcsUpdatePanelSys, ecs_view_id(PanelUpdateView));
}

EcsEntityId dev_ecs_panel_open(EcsWorld* world, const EcsEntityId window, const DevPanelType type) {
  const EcsEntityId panelEntity = dev_panel_create(world, window, type);
  DevEcsPanelComp*  ecsPanel    = ecs_world_add_t(
      world,
      panelEntity,
      DevEcsPanelComp,
      .panel        = ui_panel(.size = ui_vector(800, 500)),
      .scrollview   = ui_scrollview(),
      .nameFilter   = dynstring_create(g_allocHeap, 32),
      .compSortMode = DevCompSortMode_Archetypes,
      .archSortMode = DevArchSortMode_ChunkCount,
      .sysSortMode  = DevSysSortMode_Duration,
      .components   = dynarray_create_t(g_allocHeap, DevEcsCompInfo, 256),
      .views        = dynarray_create_t(g_allocHeap, DevEcsViewInfo, 256),
      .archetypes   = dynarray_create_t(g_allocHeap, DevEcsArchetypeInfo, 256),
      .systems      = dynarray_create_t(g_allocHeap, DevEcsSysInfo, 256));

  if (type == DevPanelType_Detached) {
    ui_panel_maximize(&ecsPanel->panel);
  }

  return panelEntity;
}
