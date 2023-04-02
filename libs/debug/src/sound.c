#include "core_format.h"
#include "debug_sound.h"
#include "ecs_world.h"
#include "snd.h"
#include "ui.h"

ecs_comp_define(DebugSoundPanelComp) { UiPanel panel; };

ecs_view_define(GlobalView) { ecs_access_write(SndMixerComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugSoundPanelComp);
  ecs_access_write(UiCanvasComp);
}

static void
sound_panel_draw(UiCanvasComp* canvas, DebugSoundPanelComp* panelComp, SndMixerComp* mixer) {

  (void)mixer;

  const String title = fmt_write_scratch("{} Sound Panel", fmt_ui_shape(MusicNote));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugSoundUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SndMixerComp* mixer = ecs_view_write_t(globalItr, SndMixerComp);

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId    entity    = ecs_view_entity(itr);
    DebugSoundPanelComp* panelComp = ecs_view_write_t(itr, DebugSoundPanelComp);
    UiCanvasComp*        canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    sound_panel_draw(canvas, panelComp, mixer);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_sound_module) {
  ecs_register_comp(DebugSoundPanelComp);

  ecs_register_view(GlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugSoundUpdatePanelSys, ecs_view_id(GlobalView), ecs_view_id(PanelUpdateView));
}

EcsEntityId debug_sound_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugSoundPanelComp,
      .panel = ui_panel(.position = ui_vector(0.75f, 0.5f), .size = ui_vector(330, 190)));
  return panelEntity;
}
