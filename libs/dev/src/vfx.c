#include "core/alloc.h"
#include "core/array.h"
#include "core/dynstring.h"
#include "core/format.h"
#include "core/stringtable.h"
#include "dev/panel.h"
#include "dev/vfx.h"
#include "ecs/entity.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "scene/id.h"
#include "scene/name.h"
#include "scene/set.h"
#include "ui/canvas.h"
#include "ui/layout.h"
#include "ui/panel.h"
#include "ui/scrollview.h"
#include "ui/shape.h"
#include "ui/style.h"
#include "ui/table.h"
#include "ui/widget.h"
#include "vfx/decal.h"
#include "vfx/stats.h"
#include "vfx/system.h"

// clang-format off

static const String g_tooltipFilter       = string_static("Filter entries by name or entity.\nSupports glob characters \a.b*\ar and \a.b?\ar (\a.b!\ar prefix to invert).");
static const String g_tooltipFreeze       = string_static("Freeze the data set (halts data collection).");
static const String g_tooltipSelectEntity = string_static("Select the entity.");

// clang-format on

typedef enum {
  VfxSortMode_Entity,
  VfxSortMode_Sprites,
  VfxSortMode_Stamps,

  VfxSortMode_Count,
} VfxSortMode;

static const String g_vfxSortModeNames[] = {
    string_static("Entity"),
    string_static("Sprites"),
    string_static("Stamps"),
};
ASSERT(array_elems(g_vfxSortModeNames) == VfxSortMode_Count, "Incorrect number of names");

typedef struct {
  StringHash  nameHash;
  EcsEntityId entity;
  i32         stats[VfxStat_Count];
} DevVfxInfo;

ecs_comp_define(DevVfxPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
  bool         freeze;
  VfxSortMode  sortMode;
  DynString    filter;
  DynArray     objects; // DevVfxInfo[]
};

static void ecs_destruct_vfx_panel(void* data) {
  DevVfxPanelComp* comp = data;
  dynstring_destroy(&comp->filter);
  dynarray_destroy(&comp->objects);
}

ecs_view_define(VfxObjView) {
  ecs_access_with(VfxStatsAnyComp);
  ecs_access_read(SceneNameComp);
  ecs_access_maybe_read(VfxSystemStatsComp);
  ecs_access_maybe_read(VfxDecalSingleStatsComp);
  ecs_access_maybe_read(VfxDecalTrailStatsComp);
}

ecs_view_define(PanelUpdateGlobalView) { ecs_access_write(SceneSetEnvComp); }

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevVfxPanelComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DevVfxPanelComp);
  ecs_access_write(UiCanvasComp);
}

static i8 vfx_compare_info_entity(const void* a, const void* b) {
  const u32 serialA = ecs_entity_id_serial(((const DevVfxInfo*)a)->entity);
  const u32 serialB = ecs_entity_id_serial(((const DevVfxInfo*)b)->entity);
  return compare_u32(&serialA, &serialB);
}

static i8 comp_compare_info_stat(const void* a, const void* b, const VfxStat stat) {
  const u32 statValA = ((const DevVfxInfo*)a)->stats[stat];
  const u32 statValB = ((const DevVfxInfo*)b)->stats[stat];
  const i8  res      = compare_u32_reverse(&statValA, &statValB);
  return res ? res : vfx_compare_info_entity(a, b);
}

static i8 comp_compare_info_sprites(const void* a, const void* b) {
  return comp_compare_info_stat(a, b, VfxStat_SpriteCount);
}

static i8 comp_compare_info_stamps(const void* a, const void* b) {
  return comp_compare_info_stat(a, b, VfxStat_StampCount);
}

static bool vfx_panel_filter(DevVfxPanelComp* panelComp, const String name, const EcsEntityId e) {
  if (string_is_empty(panelComp->filter)) {
    return true;
  }
  const String           rawFilter = dynstring_view(&panelComp->filter);
  const String           filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));
  const StringMatchFlags flags     = StringMatchFlags_IgnoreCase;
  if (string_match_glob(name, filter, flags)) {
    return true;
  }
  return string_match_glob(fmt_write_scratch("{}", ecs_entity_fmt(e)), filter, flags);
}

static String vfx_entity_name(const StringHash nameHash) {
  const String name = stringtable_lookup(g_stringtable, nameHash);
  return string_is_empty(name) ? string_lit("<unnamed>") : name;
}

static void vfx_info_stats_add(DevVfxInfo* info, const VfxStatSet* set) {
  for (VfxStat stat = 0; stat != VfxStat_Count; ++stat) {
    info->stats[stat] += set->valuesLast[stat];
  }
}

static void vfx_info_query(DevVfxPanelComp* panelComp, EcsWorld* world) {
  if (!panelComp->freeze) {
    dynarray_clear(&panelComp->objects);

    EcsView* objView = ecs_world_view_t(world, VfxObjView);
    for (EcsIterator* itr = ecs_view_itr(objView); ecs_view_walk(itr);) {
      const EcsEntityId    entity   = ecs_view_entity(itr);
      const SceneNameComp* nameComp = ecs_view_read_t(itr, SceneNameComp);

      if (!vfx_panel_filter(panelComp, vfx_entity_name(nameComp->nameDebug), entity)) {
        continue;
      }
      DevVfxInfo* info = dynarray_push_t(&panelComp->objects, DevVfxInfo);
      info->entity     = entity;
      info->nameHash   = nameComp->nameDebug;
      mem_set(mem_var(info->stats), 0);

      const VfxSystemStatsComp* systemStats = ecs_view_read_t(itr, VfxSystemStatsComp);
      if (systemStats) {
        vfx_info_stats_add(info, &systemStats->set);
      }
      const VfxDecalSingleStatsComp* decalSglStats = ecs_view_read_t(itr, VfxDecalSingleStatsComp);
      if (decalSglStats) {
        vfx_info_stats_add(info, &decalSglStats->set);
      }
      const VfxDecalTrailStatsComp* decalTrailStats = ecs_view_read_t(itr, VfxDecalTrailStatsComp);
      if (decalTrailStats) {
        vfx_info_stats_add(info, &decalTrailStats->set);
      }
    }
  }

  switch (panelComp->sortMode) {
  case VfxSortMode_Entity:
    dynarray_sort(&panelComp->objects, vfx_compare_info_entity);
    break;
  case VfxSortMode_Sprites:
    dynarray_sort(&panelComp->objects, comp_compare_info_sprites);
    break;
  case VfxSortMode_Stamps:
    dynarray_sort(&panelComp->objects, comp_compare_info_stamps);
    break;
  case VfxSortMode_Count:
    break;
  }
}

static void vfx_options_draw(UiCanvasComp* canvas, DevVfxPanelComp* panelComp) {
  ui_layout_push(canvas);
  ui_style_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 60);
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 40);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 125);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Filter:"));
  ui_table_next_column(canvas, &table);
  ui_textbox(
      canvas, &panelComp->filter, .placeholder = string_lit("*"), .tooltip = g_tooltipFilter);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Freeze:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &panelComp->freeze, .tooltip = g_tooltipFreeze);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Sort:"));
  ui_table_next_column(canvas, &table);
  ui_select(canvas, (i32*)&panelComp->sortMode, g_vfxSortModeNames, VfxSortMode_Count);

  const String stats =
      fmt_write_scratch("Count: {}", fmt_int(panelComp->objects.size, .minDigits = 4));

  ui_table_next_column(canvas, &table);
  ui_style_variation(canvas, UiVariation_Monospace);
  ui_label(canvas, stats, .selectable = true);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
}

static void
vfx_panel_draw(UiCanvasComp* canvas, DevVfxPanelComp* panelComp, SceneSetEnvComp* setEnv) {
  const String title = fmt_write_scratch("{} Vfx Panel", fmt_ui_shape(Diamond));
  ui_panel_begin(
      canvas, &panelComp->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  vfx_options_draw(canvas, panelComp);

  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None, UiLayer_Normal);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 175);
  ui_table_add_column(&table, UiTableColumn_Fixed, 215);
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
  const f32 height     = ui_table_height(&table, numObjects);
  ui_scrollview_begin(canvas, &panelComp->scrollview, UiLayer_Normal, height);

  ui_canvas_id_block_next(canvas); // Start the list of objects on its own id block.
  for (u32 objIdx = 0; objIdx != numObjects; ++objIdx) {
    const DevVfxInfo*      info = dynarray_at_t(&panelComp->objects, objIdx, DevVfxInfo);
    const f32              y    = ui_table_height(&table, objIdx);
    const UiScrollviewCull cull = ui_scrollview_cull(&panelComp->scrollview, y, table.rowHeight);
    if (cull == UiScrollviewCull_After) {
      break;
    }
    if (cull == UiScrollviewCull_Before) {
      continue;
    }

    ui_table_jump_row(canvas, &table, objIdx);

    const bool    selected = scene_set_contains(setEnv, SceneId_selected, info->entity);
    const UiColor color    = selected ? ui_color(32, 32, 255, 192) : ui_color(48, 48, 48, 192);
    ui_table_draw_row_bg(canvas, &table, color);
    ui_canvas_id_block_index(canvas, ecs_entity_id_index(info->entity) * 10); // Set a stable id.

    ui_label(canvas, vfx_entity_name(info->nameHash), .selectable = true);
    ui_table_next_column(canvas, &table);
    ui_label_entity(canvas, info->entity);

    ui_layout_push(canvas);
    ui_layout_inner(
        canvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(25, 25), UiBase_Absolute);
    if (ui_button(
            canvas,
            .label      = ui_shape_scratch(UiShape_SelectAll),
            .frameColor = ui_color(0, 16, 255, 192),
            .fontSize   = 18,
            .tooltip    = g_tooltipSelectEntity)) {
      scene_set_clear(setEnv, SceneId_selected);
      scene_set_add(setEnv, SceneId_selected, info->entity, SceneSetFlags_None);
    }

    ui_layout_pop(canvas);
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

ecs_system_define(DevVfxUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneSetEnvComp* setEnv = ecs_view_write_t(globalItr, SceneSetEnvComp);

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId entity    = ecs_view_entity(itr);
    DevVfxPanelComp*  panelComp = ecs_view_write_t(itr, DevVfxPanelComp);
    UiCanvasComp*     canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp)) && !pinned) {
      continue;
    }
    vfx_info_query(panelComp, world);
    vfx_panel_draw(canvas, panelComp, setEnv);

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(dev_vfx_module) {
  ecs_register_comp(DevVfxPanelComp, .destructor = ecs_destruct_vfx_panel);

  ecs_register_view(VfxObjView);
  ecs_register_view(PanelUpdateGlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DevVfxUpdatePanelSys,
      ecs_view_id(VfxObjView),
      ecs_view_id(PanelUpdateGlobalView),
      ecs_view_id(PanelUpdateView));
}

EcsEntityId dev_vfx_panel_open(EcsWorld* world, const EcsEntityId window, const DevPanelType type) {
  const EcsEntityId panelEntity = dev_panel_create(world, window, type);
  DevVfxPanelComp*  vfxPanel    = ecs_world_add_t(
      world,
      panelEntity,
      DevVfxPanelComp,
      .panel      = ui_panel(.size = ui_vector(850, 500)),
      .scrollview = ui_scrollview(),
      .filter     = dynstring_create(g_allocHeap, 32),
      .objects    = dynarray_create_t(g_allocHeap, DevVfxInfo, 128));

  if (type == DevPanelType_Detached) {
    ui_panel_maximize(&vfxPanel->panel);
  }

  return panelEntity;
}
