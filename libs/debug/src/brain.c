#include "ai_blackboard.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_stringtable.h"
#include "debug_brain.h"
#include "debug_register.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_brain.h"
#include "scene_selection.h"
#include "ui.h"

typedef enum {
  DebugBrainTab_Blackboard,

  DebugBrainTab_Count,
} DebugBrainTab;

static const String g_brainTabNames[] = {
    string_static("Blackboard"),
};
ASSERT(array_elems(g_brainTabNames) == DebugBrainTab_Count, "Incorrect number of names");

typedef struct {
  StringHash key;
  String     name;
} DebugBlackboardEntry;

ecs_comp_define(DebugBrainPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
};

static i8 blackboard_compare_entry_name(const void* a, const void* b) {
  return compare_string(
      field_ptr(a, DebugBlackboardEntry, name), field_ptr(b, DebugBlackboardEntry, name));
}

ecs_view_define(SubjectView) { ecs_access_write(SceneBrainComp); }

static void blackboard_draw_bool(UiCanvasComp* canvas, AiBlackboard* bb, const StringHash key) {
  bool val = ai_blackboard_get_bool(bb, key);
  if (ui_toggle(canvas, &val)) {
    ai_blackboard_set_bool(bb, key, val);
  }
}

static void blackboard_draw_f64(UiCanvasComp* canvas, AiBlackboard* bb, const StringHash key) {
  f64 val = ai_blackboard_get_f64(bb, key);
  if (ui_numbox(canvas, &val, .min = f64_min, .max = f64_max)) {
    ai_blackboard_set_f64(bb, key, val);
  }
}

static void blackboard_draw_vector(UiCanvasComp* canvas, AiBlackboard* bb, const StringHash key) {
  static const f32 g_spacing = 10.0f;
  const UiAlign    align     = UiAlign_MiddleLeft;
  ui_layout_push(canvas);
  ui_layout_resize(canvas, align, ui_vector(1.0f / 4, 0), UiBase_Current, Ui_X);
  ui_layout_grow(canvas, align, ui_vector(3 * -g_spacing / 4, 0), UiBase_Absolute, Ui_X);

  GeoVector val = ai_blackboard_get_vector(bb, key);
  for (u8 comp = 0; comp != 4; ++comp) {
    f64 compVal = val.comps[comp];
    if (ui_numbox(canvas, &compVal, .min = f32_min, .max = f32_max)) {
      val.comps[comp] = (f32)compVal;
      ai_blackboard_set_vector(bb, key, val);
    }
    ui_layout_next(canvas, Ui_Right, g_spacing);
  }
  ui_layout_pop(canvas);
}

static void blackboard_draw_time(UiCanvasComp* canvas, AiBlackboard* bb, const StringHash key) {
  const TimeDuration val = ai_blackboard_get_time(bb, key);
  ui_label(canvas, fmt_write_scratch("{}", fmt_duration(val)));
}

static void blackboard_draw_entity(UiCanvasComp* canvas, AiBlackboard* bb, const StringHash key) {
  const EcsEntityId val = ai_blackboard_get_entity(bb, key);
  ui_label_entity(canvas, val);
}

static void blackboard_options_draw(UiCanvasComp* canvas, SceneBrainComp* brain) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 135);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 155);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);

  ui_table_next_row(canvas, &table);
  bool pauseSensors = (scene_brain_flags(brain) & SceneBrainFlags_PauseSensors) != 0;
  ui_label(canvas, string_lit("Pause sensors:"));
  ui_table_next_column(canvas, &table);
  if (ui_toggle(canvas, &pauseSensors)) {
    scene_brain_flags_toggle(brain, SceneBrainFlags_PauseSensors);
  }

  ui_table_next_column(canvas, &table);
  bool pauseControllers = (scene_brain_flags(brain) & SceneBrainFlags_PauseControllers) != 0;
  ui_label(canvas, string_lit("Pause controllers:"));
  ui_table_next_column(canvas, &table);
  if (ui_toggle(canvas, &pauseControllers)) {
    scene_brain_flags_toggle(brain, SceneBrainFlags_PauseControllers);
  }

  ui_layout_pop(canvas);
}

static void blackboard_panel_tab_draw(
    UiCanvasComp* canvas, DebugBrainPanelComp* panelComp, EcsIterator* subject) {
  diag_assert(subject);

  SceneBrainComp* brain = ecs_view_write_t(subject, SceneBrainComp);
  AiBlackboard*   bb    = scene_brain_blackboard_mutable(brain);

  blackboard_options_draw(canvas, brain);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Key"), string_lit("Knowledge key.")},
          {string_lit("Type"), string_lit("Knowledge type.")},
          {string_lit("Value"), string_lit("Knowledge value.")},
      });

  // Collect the blackboard entries.
  DynArray entries = dynarray_create_t(g_alloc_scratch, DebugBlackboardEntry, 256);
  for (AiBlackboardItr itr = ai_blackboard_begin(bb); itr.key; itr = ai_blackboard_next(bb, itr)) {
    const String name                                = stringtable_lookup(g_stringtable, itr.key);
    *dynarray_push_t(&entries, DebugBlackboardEntry) = (DebugBlackboardEntry){
        .key  = itr.key,
        .name = string_is_empty(name) ? string_lit("<unnamed>") : name,
    };
  }

  // Sort the blackboard entries.
  dynarray_sort(&entries, blackboard_compare_entry_name);

  // Draw the blackboard entries.
  const f32 totalHeight = ui_table_height(&table, (u32)entries.size);
  ui_scrollview_begin(canvas, &panelComp->scrollview, totalHeight);

  if (entries.size) {
    dynarray_for_t(&entries, DebugBlackboardEntry, entry) {
      const AiBlackboardType type = ai_blackboard_type(bb, entry->key);

      ui_table_next_row(canvas, &table);
      ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));

      ui_label(canvas, entry->name);
      ui_table_next_column(canvas, &table);

      ui_label(canvas, ai_blackboard_type_str(type));
      ui_table_next_column(canvas, &table);

      switch (type) {
      case AiBlackboardType_f64:
        blackboard_draw_f64(canvas, bb, entry->key);
        break;
      case AiBlackboardType_Bool:
        blackboard_draw_bool(canvas, bb, entry->key);
        break;
      case AiBlackboardType_Vector:
        blackboard_draw_vector(canvas, bb, entry->key);
        break;
      case AiBlackboardType_Time:
        blackboard_draw_time(canvas, bb, entry->key);
        break;
      case AiBlackboardType_Entity:
        blackboard_draw_entity(canvas, bb, entry->key);
        break;
      case AiBlackboardType_Invalid:
      case AiBlackboardType_Count:
        diag_crash();
      }
    }
  } else {
    ui_label(
        canvas, string_lit("Blackboard has no knowledge entries."), .align = UiAlign_MiddleCenter);
  }

  dynarray_destroy(&entries);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void
brain_panel_draw(UiCanvasComp* canvas, DebugBrainPanelComp* panelComp, EcsIterator* subject) {
  const String title = fmt_write_scratch("{} Brain Panel", fmt_ui_shape(Psychology));
  ui_panel_begin(
      canvas,
      &panelComp->panel,
      .title    = title,
      .tabNames = g_brainTabNames,
      .tabCount = DebugBrainTab_Count);

  if (subject) {
    switch (panelComp->panel.activeTab) {
    case DebugBrainTab_Blackboard:
      blackboard_panel_tab_draw(canvas, panelComp, subject);
      break;
    }
  } else {
    ui_label(canvas, string_lit("Select an entity with a brain."), .align = UiAlign_MiddleCenter);
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_view_define(PanelUpdateGlobalView) { ecs_access_read(SceneSelectionComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugBrainPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DebugBrainUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneSelectionComp* selection = ecs_view_read_t(globalItr, SceneSelectionComp);

  EcsView*     subjectView = ecs_world_view_t(world, SubjectView);
  EcsIterator* subject     = ecs_view_maybe_at(subjectView, scene_selection_main(selection));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugBrainPanelComp* panelComp = ecs_view_write_t(itr, DebugBrainPanelComp);
    UiCanvasComp*        canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    brain_panel_draw(canvas, panelComp, subject);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_brain_module) {
  ecs_register_comp(DebugBrainPanelComp);

  ecs_register_view(PanelUpdateGlobalView);
  ecs_register_view(PanelUpdateView);
  ecs_register_view(SubjectView);

  ecs_register_system(
      DebugBrainUpdatePanelSys,
      ecs_view_id(PanelUpdateGlobalView),
      ecs_view_id(PanelUpdateView),
      ecs_view_id(SubjectView));
}

EcsEntityId debug_brain_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, panelEntity, DebugBrainPanelComp, .panel = ui_panel(.size = ui_vector(600, 500)));
  return panelEntity;
}
