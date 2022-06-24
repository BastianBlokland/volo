#include "core_stringtable.h"
#include "debug_animation.h"
#include "debug_register.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_skeleton.h"
#include "ui.h"

ecs_comp_define(DebugAnimationPanelComp) { UiPanel panel; };

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugAnimationPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(AnimationView) { ecs_access_write(SceneAnimationComp); }

static void animation_panel_draw(
    UiCanvasComp* canvas, DebugAnimationPanelComp* panelComp, SceneAnimationComp* anim) {
  const String title = fmt_write_scratch("{} Animation Debug", fmt_ui_shape(Animation));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  if (anim) {
    UiTable table = ui_table(.spacing = ui_vector(10, 5));
    ui_table_add_column(&table, UiTableColumn_Fixed, 100);
    ui_table_add_column(&table, UiTableColumn_Fixed, 50);
    ui_table_add_column(&table, UiTableColumn_Flexible, 0);

    for (u32 layerIdx = 0; layerIdx != anim->layerCount; ++layerIdx) {
      SceneAnimLayer* layer = &anim->layers[layerIdx];
      const String    name  = stringtable_lookup(g_stringtable, layer->nameHash);

      ui_table_next_row(canvas, &table);
      ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));

      ui_label(canvas, string_is_empty(name) ? string_lit("<unnamed>") : name, .selectable = true);
      ui_table_next_column(canvas, &table);

      ui_label(
          canvas,
          fmt_write_scratch("{}", fmt_float(layer->time, .minDecDigits = 2, .maxDecDigits = 2)));
      ui_table_next_column(canvas, &table);
    }
  } else {
    ui_label(canvas, string_lit("No animated entities found"), .align = UiAlign_MiddleCenter);
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugAnimationUpdatePanelSys) {
  EcsIterator* animItr = ecs_view_itr(ecs_world_view_t(world, AnimationView));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugAnimationPanelComp* panelComp = ecs_view_write_t(itr, DebugAnimationPanelComp);
    UiCanvasComp*            canvas    = ecs_view_write_t(itr, UiCanvasComp);

    SceneAnimationComp* anim = null;

    ecs_view_itr_reset(animItr);
    if (ecs_view_walk(animItr)) {
      /**
       * NOTE: At the moment we take the first animated object to debug, in the future a picker
       * should be implemented.
       */
      anim = ecs_view_write_t(animItr, SceneAnimationComp);
    }

    ui_canvas_reset(canvas);
    animation_panel_draw(canvas, panelComp, anim);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_animation_module) {
  ecs_register_comp(DebugAnimationPanelComp);

  ecs_register_view(PanelUpdateView);
  ecs_register_view(AnimationView);

  ecs_register_system(
      DebugAnimationUpdatePanelSys, ecs_view_id(PanelUpdateView), ecs_view_id(AnimationView));
}

EcsEntityId debug_animation_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, panelEntity, DebugAnimationPanelComp, .panel = ui_panel(ui_vector(450, 300)));
  return panelEntity;
}
