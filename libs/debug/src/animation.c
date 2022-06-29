#include "core_math.h"
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
  u32          totalRows;
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

static void
anim_draw_vec(UiCanvasComp* canvas, const GeoVector v, const u8 digits, const String tooltip) {
  ui_label(
      canvas,
      fmt_write_scratch(
          "{>4} {>4} {>4}",
          fmt_float(v.x, .minDecDigits = digits, .maxDecDigits = digits, .expThresholdNeg = 0),
          fmt_float(v.y, .minDecDigits = digits, .maxDecDigits = digits, .expThresholdNeg = 0),
          fmt_float(v.z, .minDecDigits = digits, .maxDecDigits = digits, .expThresholdNeg = 0)),
      .tooltip  = tooltip,
      .fontSize = 12);
}

static void anim_draw_quat(UiCanvasComp* canvas, const GeoQuat q, const String tooltip) {
  const GeoVector angles = geo_quat_to_euler(q);
  ui_label(
      canvas,
      fmt_write_scratch(
          "{>4} {>4} {>4}",
          fmt_float(angles.x * math_rad_to_deg, .maxDecDigits = 0, .expThresholdNeg = 0),
          fmt_float(angles.y * math_rad_to_deg, .maxDecDigits = 0, .expThresholdNeg = 0),
          fmt_float(angles.z * math_rad_to_deg, .maxDecDigits = 0, .expThresholdNeg = 0)),
      .tooltip  = tooltip,
      .fontSize = 12);
}

static void anim_draw_pose(UiCanvasComp* canvas, UiTable* table, const SceneJointPose pose) {
  anim_draw_vec(canvas, pose.t, 1, string_lit("Translation."));
  ui_table_next_column(canvas, table);
  anim_draw_quat(canvas, pose.r, string_lit("Rotation."));
  ui_table_next_column(canvas, table);
  anim_draw_vec(canvas, pose.s, 2, string_lit("Scale."));
  ui_table_next_column(canvas, table);
}

static void anim_draw_pose_animated(
    UiCanvasComp* canvas, UiTable* table, const SceneJointPose pose, const SceneJointInfo info) {
  if (info.frameCountT) {
    const u32 count = info.frameCountT;
    anim_draw_vec(
        canvas, pose.t, 1, fmt_write_scratch("Translation.\nFrames: {}.", fmt_int(count)));
  }
  ui_table_next_column(canvas, table);
  if (info.frameCountR) {
    const u32 count = info.frameCountR;
    anim_draw_quat(canvas, pose.r, fmt_write_scratch("Rotation.\nFrames: {}.", fmt_int(count)));
  }
  ui_table_next_column(canvas, table);
  if (info.frameCountS) {
    const u32 count = info.frameCountS;
    anim_draw_vec(canvas, pose.s, 2, fmt_write_scratch("Scale.\nFrames: {}.", fmt_int(count)));
  }
}

static void anim_draw_joints_layer(
    UiCanvasComp*                 canvas,
    UiTable*                      table,
    SceneAnimLayer*               layer,
    const u32                     layerIdx,
    const SceneSkeletonTemplComp* skelTempl) {

  ui_style_push(canvas);
  ui_style_variation(canvas, UiVariation_Monospace);

  u32 stack[scene_skeleton_joints_max] = {scene_skeleton_root_index(skelTempl)};
  u32 depth[scene_skeleton_joints_max] = {0};
  u32 stackCount                       = 1;

  while (stackCount--) {
    const u32                 jointIdx = stack[stackCount];
    const SceneSkeletonJoint* joint    = scene_skeleton_joint(skelTempl, jointIdx);
    const SceneJointInfo      info     = scene_skeleton_info(skelTempl, layerIdx, jointIdx);
    const String              name     = stringtable_lookup(g_stringtable, joint->nameHash);

    ui_table_next_row(canvas, table);
    ui_table_draw_row_bg(canvas, table, ui_color(96, 96, 96, 192));

    bool enabled = scene_skeleton_mask_test(&layer->mask, jointIdx);
    if (ui_toggle(canvas, &enabled, .tooltip = string_lit("Enable / disable this joint."))) {
      scene_skeleton_mask_flip(&layer->mask, jointIdx);
    }

    ui_label(
        canvas,
        fmt_write_scratch("{}{}", fmt_padding(4 + depth[stackCount]), fmt_text(name)),
        .fontSize = 12);
    ui_table_next_column(canvas, table);

    const SceneJointPose pose = scene_skeleton_sample(skelTempl, layerIdx, jointIdx, layer->time);
    anim_draw_pose_animated(canvas, table, pose, info);
    ui_table_next_column(canvas, table);

    const u32 childDepth = depth[stackCount] + 1;
    for (u32 childNum = 0; childNum != joint->childCount; ++childNum) {
      stack[stackCount]   = joint->childIndices[childNum];
      depth[stackCount++] = childDepth;
    }
  }

  ui_style_pop(canvas);
}

static void anim_draw_joints_def(
    UiCanvasComp* canvas, UiTable* table, const SceneSkeletonTemplComp* skelTempl) {

  ui_style_push(canvas);
  ui_style_variation(canvas, UiVariation_Monospace);

  ui_table_next_row(canvas, table);
  ui_table_draw_row_bg(canvas, table, ui_color(96, 96, 96, 192));
  ui_label(canvas, string_lit("<root>"), .fontSize = 12);
  ui_table_next_column(canvas, table);

  const SceneJointPose rootPose = scene_skeleton_root(skelTempl);
  anim_draw_pose(canvas, table, rootPose);

  u32 stack[scene_skeleton_joints_max] = {scene_skeleton_root_index(skelTempl)};
  u32 depth[scene_skeleton_joints_max] = {1};
  u32 stackCount                       = 1;

  while (stackCount--) {
    const u32                 jointIdx = stack[stackCount];
    const SceneSkeletonJoint* joint    = scene_skeleton_joint(skelTempl, jointIdx);
    const String              name     = stringtable_lookup(g_stringtable, joint->nameHash);

    ui_table_next_row(canvas, table);
    ui_table_draw_row_bg(canvas, table, ui_color(96, 96, 96, 192));

    ui_label(
        canvas,
        fmt_write_scratch("{}{}", fmt_padding(depth[stackCount]), fmt_text(name)),
        .fontSize = 12);
    ui_table_next_column(canvas, table);

    const SceneJointPose pose = scene_skeleton_sample_def(skelTempl, jointIdx);
    anim_draw_pose(canvas, table, pose);

    const u32 childDepth = depth[stackCount] + 1;
    for (u32 childNum = 0; childNum != joint->childCount; ++childNum) {
      stack[stackCount]   = joint->childIndices[childNum];
      depth[stackCount++] = childDepth;
    }
  }

  ui_style_pop(canvas);
}

static void anim_panel_draw(
    UiCanvasComp*                 canvas,
    DebugAnimationPanelComp*      panelComp,
    SceneAnimationComp*           anim,
    const SceneSkeletonTemplComp* skelTempl) {
  const String title = fmt_write_scratch("{} Animation Debug", fmt_ui_shape(Animation));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  if (anim) {
    UiTable table = ui_table(.spacing = ui_vector(10, 5));
    ui_table_add_column(&table, UiTableColumn_Fixed, 300);
    ui_table_add_column(&table, UiTableColumn_Fixed, 140);
    ui_table_add_column(&table, UiTableColumn_Fixed, 150);
    ui_table_add_column(&table, UiTableColumn_Fixed, 140);
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

    const f32 totalHeight = ui_table_height(&table, panelComp->totalRows);
    ui_scrollview_begin(canvas, &panelComp->scrollview, totalHeight);
    panelComp->totalRows = 1; // Always draws the default layer.

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
        anim_draw_joints_layer(canvas, &table, layer, layerIdx, skelTempl);
      }

      panelComp->totalRows += 1 + (open ? scene_skeleton_joint_count(skelTempl) : 0);

      ui_canvas_id_block_next(canvas); // Use a consistent amount of ids regardless of if 'open'.
    }

    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));
    if (ui_section(canvas, .label = string_lit("<default>"))) {
      anim_draw_joints_def(canvas, &table, skelTempl);
      panelComp->totalRows += scene_skeleton_joint_count(skelTempl) + 1;
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
    anim_panel_draw(canvas, panelComp, anim, skelTempl);

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
