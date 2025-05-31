#include "core_alloc.h"
#include "core_dynarray.h"
#include "core_format.h"
#include "core_stringtable.h"
#include "dev_hierarchy.h"
#include "dev_panel.h"
#include "ecs_entity.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "input_manager.h"
#include "scene.h"
#include "scene_name.h"
#include "scene_set.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_scrollview.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_table.h"
#include "ui_widget.h"

typedef struct {
  EcsEntityId entity;
  StringHash  nameHash;
} HierarchyEntry;

ecs_comp_define(DevHierarchyPanelComp) {
  UiPanel      panel;
  u32          panelRowCount;
  UiScrollview scrollview;
  DynArray     entries; // HierarchyEntry[]

  EcsEntityId lastMainSelection;
};

static void ecs_destruct_hierarchy_panel(void* data) {
  DevHierarchyPanelComp* comp = data;
  dynarray_destroy(&comp->entries);
}

ecs_view_define(HierarchyEntryView) {
  ecs_access_with(SceneLevelInstanceComp);
  ecs_access_read(SceneNameComp);
}

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_write(SceneSetEnvComp);
  ecs_access_read(InputManagerComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevHierarchyPanelComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DevHierarchyPanelComp);
  ecs_access_write(UiCanvasComp);
}

static i8 hierarchy_compare_entry(const void* a, const void* b) {
  const EcsEntityId entityA = ((const HierarchyEntry*)a)->entity;
  const EcsEntityId entityB = ((const HierarchyEntry*)b)->entity;
  return entityA < entityB ? -1 : entityA > entityB ? 1 : 0;
}

static void hierarchy_query(DevHierarchyPanelComp* panelComp, EcsWorld* world) {
  dynarray_clear(&panelComp->entries);

  EcsView* entryView = ecs_world_view_t(world, HierarchyEntryView);
  for (EcsIterator* itr = ecs_view_itr(entryView); ecs_view_walk(itr);) {
    const EcsEntityId    entity   = ecs_view_entity(itr);
    const SceneNameComp* nameComp = ecs_view_read_t(itr, SceneNameComp);

    HierarchyEntry* entry = dynarray_push_t(&panelComp->entries, HierarchyEntry);
    entry->entity         = entity;
    entry->nameHash       = nameComp->name;
  }

  dynarray_sort(&panelComp->entries, hierarchy_compare_entry);
}

static String hierarchy_name(const StringHash nameHash) {
  const String name = stringtable_lookup(g_stringtable, nameHash);
  return string_is_empty(name) ? string_lit("<unnamed>") : name;
}

static UiColor hierarchy_bg_color(const HierarchyEntry* entry, const bool selected) {
  (void)entry;
  if (selected) {
    return ui_color(48, 48, 178, 192);
  }
  return ui_color(48, 48, 48, 192);
}

static void hierarchy_panel_draw(
    UiCanvasComp*           canvas,
    DevHierarchyPanelComp*  panelComp,
    SceneSetEnvComp*        setEnv,
    const InputManagerComp* input,
    EcsWorld*               world,
    const EcsEntityId       focusEntity) {
  const String title = fmt_write_scratch("{} Hierarchy Panel", fmt_ui_shape(Tree));
  ui_panel_begin(
      canvas, &panelComp->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  const f32 height = ui_table_height(&table, panelComp->panelRowCount);
  ui_scrollview_begin(canvas, &panelComp->scrollview, UiLayer_Normal, height);
  ui_canvas_id_block_next(canvas); // Start the list of entities on its own id block.

  panelComp->panelRowCount = 0;
  for (u32 entryIdx = 0; entryIdx != panelComp->entries.size; ++entryIdx) {
    const HierarchyEntry* entry = dynarray_at_t(&panelComp->entries, entryIdx, HierarchyEntry);

    ui_table_next_row(canvas, &table);

    const f32              y    = ui_table_height(&table, panelComp->panelRowCount++);
    const UiScrollviewCull cull = ui_scrollview_cull(&panelComp->scrollview, y, table.rowHeight);

    if (focusEntity == entry->entity && cull) {
      panelComp->scrollview.offset = y;
    }
    if (cull) {
      continue;
    }
    const bool selected = scene_set_contains(setEnv, g_sceneSetSelected, entry->entity);

    ui_table_draw_row_bg(canvas, &table, hierarchy_bg_color(entry, selected));
    ui_style_push(canvas);
    if (selected) {
      ui_style_outline(canvas, 2);
    }
    ui_label(canvas, hierarchy_name(entry->nameHash), .selectable = true);
    ui_style_pop(canvas);

    ui_layout_push(canvas);
    ui_layout_inner(
        canvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(25, 25), UiBase_Absolute);
    if (ui_button(
            canvas,
            .label      = ui_shape_scratch(UiShape_SelectAll),
            .fontSize   = 18,
            .frameColor = ui_color(0, 16, 255, 192),
            .tooltip    = string_static("Select the entity."))) {
      const InputModifier modifiers = input_modifiers(input);
      if (!(modifiers & (InputModifier_Control | InputModifier_Shift))) {
        scene_set_clear(setEnv, g_sceneSetSelected);
      }
      if (modifiers & InputModifier_Shift) {
        scene_set_remove(setEnv, g_sceneSetSelected, entry->entity);
      } else {
        scene_set_add(setEnv, g_sceneSetSelected, entry->entity, SceneSetFlags_None);
      }
    }
    ui_layout_next(canvas, Ui_Left, 10);
    if (ui_button(
            canvas,
            .label      = ui_shape_scratch(UiShape_Delete),
            .fontSize   = 18,
            .frameColor = ui_color(255, 16, 0, 192),
            .tooltip    = string_lit("Destroy the entity."))) {
      ecs_world_entity_destroy(world, entry->entity);
    }
    ui_layout_pop(canvas);
  }
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DevHierarchyUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneSetEnvComp*        setEnv = ecs_view_write_t(globalItr, SceneSetEnvComp);
  const InputManagerComp* input  = ecs_view_read_t(globalItr, InputManagerComp);

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
    EcsEntityId focusEntity = 0;
    if (panelComp->lastMainSelection != scene_set_main(setEnv, g_sceneSetSelected)) {
      panelComp->lastMainSelection = scene_set_main(setEnv, g_sceneSetSelected);
      focusEntity                  = panelComp->lastMainSelection;
    }
    hierarchy_query(panelComp, world);
    hierarchy_panel_draw(canvas, panelComp, setEnv, input, world, focusEntity);

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

  ecs_register_system(
      DevHierarchyUpdatePanelSys,
      ecs_register_view(PanelUpdateGlobalView),
      ecs_register_view(PanelUpdateView),
      ecs_register_view(HierarchyEntryView));
}

EcsEntityId
dev_hierarchy_panel_open(EcsWorld* world, const EcsEntityId window, const DevPanelType type) {
  const EcsEntityId      panelEntity    = dev_panel_create(world, window, type);
  DevHierarchyPanelComp* hierarchyPanel = ecs_world_add_t(
      world,
      panelEntity,
      DevHierarchyPanelComp,
      .panel      = ui_panel(.position = ui_vector(1.0f, 0.0f), .size = ui_vector(500, 350)),
      .scrollview = ui_scrollview(),
      .entries    = dynarray_create_t(g_allocHeap, HierarchyEntry, 1024));

  if (type == DevPanelType_Detached) {
    ui_panel_maximize(&hierarchyPanel->panel);
  }

  return panelEntity;
}
