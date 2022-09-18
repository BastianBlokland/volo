#include "core_array.h"
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

ecs_comp_define(DebugBrainPanelComp) { UiPanel panel; };

ecs_view_define(SubjectView) { ecs_access_read(SceneBrainComp); }

static void
brain_panel_draw(UiCanvasComp* canvas, DebugBrainPanelComp* panelComp, EcsIterator* subject) {
  const String title = fmt_write_scratch("{} Brain Panel", fmt_ui_shape(Psychology));
  ui_panel_begin(
      canvas,
      &panelComp->panel,
      .title    = title,
      .tabNames = g_brainTabNames,
      .tabCount = DebugBrainTab_Count);

  (void)subject;

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
      world, panelEntity, DebugBrainPanelComp, .panel = ui_panel(.size = ui_vector(600, 350)));
  return panelEntity;
}
