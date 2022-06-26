#include "core_stringtable.h"
#include "debug_animation.h"
#include "debug_register.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "ui.h"
#include "ui_style.h"

ecs_comp_define(DebugAnimationPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
};

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugAnimationPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(AnimationView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_write(SceneAnimationComp);
}

ecs_view_define(SkeletonTemplView) { ecs_access_read(SceneSkeletonTemplComp); }

static void animation_panel_draw_joints(
    UiCanvasComp*                 canvas,
    UiTable*                      table,
    SceneAnimLayer*               layer,
    const u32                     layerIdx,
    const SceneSkeletonTemplComp* skelTempl) {

  u32 stack[scene_skeleton_joints_max] = {scene_skeleton_root_index(skelTempl)};
  u32 depth[scene_skeleton_joints_max] = {0};
  u32 stackCount                       = 1;

  while (stackCount--) {
    const u32                 jointIdx = stack[stackCount];
    const SceneSkeletonJoint* joint    = scene_skeleton_joint(skelTempl, jointIdx);
    const SceneAnimJointInfo  info     = scene_skeleton_anim_info(skelTempl, layerIdx, jointIdx);
    const String              name     = stringtable_lookup(g_stringtable, joint->nameHash);

    ui_table_next_row(canvas, table);
    ui_table_draw_row_bg(canvas, table, ui_color(96, 96, 96, 192));

    bool enabled = scene_skeleton_mask_test(&layer->mask, jointIdx);
    if (ui_toggle(canvas, &enabled, .tooltip = string_lit("Enable / disable this joint."))) {
      scene_skeleton_mask_flip(&layer->mask, jointIdx);
    }

    const u32 padding = 6 + depth[stackCount] * 2;
    ui_label(canvas, fmt_write_scratch("{}{}", fmt_padding(padding), fmt_text(name)));
    ui_table_next_column(canvas, table);

    ui_label(
        canvas,
        fmt_write_scratch(
            "{} / {} / {}",
            fmt_int(info.frameCountT, .minDigits = 2),
            fmt_int(info.frameCountR, .minDigits = 2),
            fmt_int(info.frameCountS, .minDigits = 2)),
        .tooltip = string_lit("Frames (Translation / Rotation / Scale)"));

    const u32 childDepth = depth[stackCount] + 1;
    for (u32 childNum = 0; childNum != joint->childCount; ++childNum) {
      stack[stackCount]   = joint->childIndices[childNum];
      depth[stackCount++] = childDepth;
    }
  }
}

static void animation_panel_draw(
    UiCanvasComp*                 canvas,
    DebugAnimationPanelComp*      panelComp,
    SceneAnimationComp*           anim,
    const SceneSkeletonTemplComp* skelTempl) {
  const String title = fmt_write_scratch("{} Animation Debug", fmt_ui_shape(Animation));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  if (anim) {
    UiTable table = ui_table(.spacing = ui_vector(10, 5));
    ui_table_add_column(&table, UiTableColumn_Fixed, 300);
    ui_table_add_column(&table, UiTableColumn_Fixed, 125);
    ui_table_add_column(&table, UiTableColumn_Fixed, 125);
    ui_table_add_column(&table, UiTableColumn_Fixed, 125);
    ui_table_add_column(&table, UiTableColumn_Flexible, 0);

    ui_table_draw_header(
        canvas,
        &table,
        (const UiTableColumnName[]){
            {string_lit("Name"), string_lit("Animation name.")},
            {string_lit("Time"), string_lit("Current playback time.")},
            {string_lit("Progress"), string_lit("Current playback progress.")},
            {string_lit("Speed"), string_lit("Current playback speed.")},
            {string_lit("Weight"), string_lit("Current playback weight.")},
        });

    const u32 maxEntries = anim->layerCount * scene_skeleton_joint_count(skelTempl);
    ui_scrollview_begin(canvas, &panelComp->scrollview, ui_table_height(&table, maxEntries));

    for (u32 layerIdx = 0; layerIdx != anim->layerCount; ++layerIdx) {
      SceneAnimLayer* layer = &anim->layers[layerIdx];
      const String    name  = stringtable_lookup(g_stringtable, layer->nameHash);

      ui_table_next_row(canvas, &table);
      ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));

      const bool open =
          ui_section(canvas, .label = string_is_empty(name) ? string_lit("<unnamed>") : name);
      ui_table_next_column(canvas, &table);

      ui_label(
          canvas,
          fmt_write_scratch(
              "{} / {}",
              fmt_float(layer->time, .minDecDigits = 2, .maxDecDigits = 2),
              fmt_float(layer->duration, .minDecDigits = 2, .maxDecDigits = 2)));
      ui_table_next_column(canvas, &table);

      ui_slider(canvas, &layer->time, .max = layer->duration);
      ui_table_next_column(canvas, &table);

      ui_slider(canvas, &layer->speed, .max = 5);
      ui_table_next_column(canvas, &table);

      ui_slider(canvas, &layer->weight);
      ui_table_next_column(canvas, &table);

      if (open) {
        animation_panel_draw_joints(canvas, &table, layer, layerIdx, skelTempl);
      }
    }
    ui_scrollview_end(canvas, &panelComp->scrollview);
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

    SceneAnimationComp*           anim      = null;
    const SceneSkeletonTemplComp* skelTempl = null;

    ecs_view_itr_reset(animItr);
    if (ecs_view_walk(animItr)) {
      /**
       * NOTE: At the moment we take the first animated object to debug, in the future a picker
       * should be implemented.
       */
      anim                      = ecs_view_write_t(animItr, SceneAnimationComp);
      const EcsEntityId graphic = ecs_view_read_t(animItr, SceneRenderableComp)->graphic;
      skelTempl = ecs_utils_read_t(world, SkeletonTemplView, graphic, SceneSkeletonTemplComp);
    }

    ui_canvas_reset(canvas);
    animation_panel_draw(canvas, panelComp, anim, skelTempl);

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
  ecs_register_view(SkeletonTemplView);

  ecs_register_system(
      DebugAnimationUpdatePanelSys,
      ecs_view_id(PanelUpdateView),
      ecs_view_id(AnimationView),
      ecs_view_id(SkeletonTemplView));
}

EcsEntityId debug_animation_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, panelEntity, DebugAnimationPanelComp, .panel = ui_panel(ui_vector(875, 300)));
  return panelEntity;
}
