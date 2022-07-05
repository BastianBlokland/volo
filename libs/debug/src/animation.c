#include "core_math.h"
#include "core_stringtable.h"
#include "debug_animation.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "debug_text.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_transform.h"
#include "ui.h"

typedef enum {
  DebugAnimationFlags_DrawSkeleton        = 1 << 0,
  DebugAnimationFlags_DrawJointTransforms = 1 << 1,
  DebugAnimationFlags_DrawJointNames      = 1 << 2,
} DebugAnimationFlags;

ecs_comp_define(DebugAnimationSettingsComp) {
  DebugAnimationFlags flags;
  EcsEntityId         subject; // Animated entity being debugged.
};

ecs_comp_define(DebugAnimationPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
  u32          totalRows;
};

ecs_view_define(SettingsWriteView) { ecs_access_write(DebugAnimationSettingsComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugAnimationPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(AnimationView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_read(SceneSkeletonComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_write(SceneAnimationComp);
}

ecs_view_define(SkeletonTemplView) { ecs_access_read(SceneSkeletonTemplComp); }

ecs_view_define(GlobalDrawView) {
  ecs_access_read(DebugAnimationSettingsComp);
  ecs_access_write(DebugShapeComp);
  ecs_access_write(DebugTextComp);
}

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
  ui_style_push(canvas);
  if (info.frameCountT) {
    const u32 count = info.frameCountT;
    ui_style_color(canvas, count > 1 ? ui_color_yellow : ui_color_white);
    anim_draw_vec(
        canvas, pose.t, 1, fmt_write_scratch("Translation.\nFrames: {}.", fmt_int(count)));
  }
  ui_table_next_column(canvas, table);
  if (info.frameCountR) {
    const u32 count = info.frameCountR;
    ui_style_color(canvas, count > 1 ? ui_color_yellow : ui_color_white);
    anim_draw_quat(canvas, pose.r, fmt_write_scratch("Rotation.\nFrames: {}.", fmt_int(count)));
  }
  ui_table_next_column(canvas, table);
  if (info.frameCountS) {
    const u32 count = info.frameCountS;
    ui_style_color(canvas, count > 1 ? ui_color_yellow : ui_color_white);
    anim_draw_vec(canvas, pose.s, 2, fmt_write_scratch("Scale.\nFrames: {}.", fmt_int(count)));
  }
  ui_style_pop(canvas);
}

static void anim_draw_joints_layer(
    UiCanvasComp*                 canvas,
    UiTable*                      table,
    SceneAnimLayer*               layer,
    const u32                     layerIdx,
    const SceneSkeletonTemplComp* skelTempl) {

  ui_style_push(canvas);
  ui_style_variation(canvas, UiVariation_Monospace);

  u32 depthLookup[scene_skeleton_joints_max] = {0};

  const u32 jointCount = scene_skeleton_joint_count(skelTempl);
  for (u32 joint = 0; joint != jointCount; ++joint) {
    const StringHash     nameHash = scene_skeleton_joint_name(skelTempl, joint);
    const String         name     = stringtable_lookup(g_stringtable, nameHash);
    const SceneJointInfo info     = scene_skeleton_info(skelTempl, layerIdx, joint);

    ui_table_next_row(canvas, table);
    ui_table_draw_row_bg(canvas, table, ui_color(96, 96, 96, 192));

    bool enabled = scene_skeleton_mask_test(&layer->mask, joint);
    if (ui_toggle(canvas, &enabled, .tooltip = string_lit("Enable / disable this joint."))) {
      scene_skeleton_mask_flip(&layer->mask, joint);
    }

    const u32 parent = scene_skeleton_joint_parent(skelTempl, joint);
    const u32 depth = depthLookup[joint] = depthLookup[parent] + 1;
    ui_label(
        canvas, fmt_write_scratch("{}{}", fmt_padding(4 + depth), fmt_text(name)), .fontSize = 12);
    ui_table_next_column(canvas, table);

    const SceneJointPose pose = scene_skeleton_sample(skelTempl, layerIdx, joint, layer->time);
    anim_draw_pose_animated(canvas, table, pose, info);
    ui_table_next_column(canvas, table);
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

  u32 depthLookup[scene_skeleton_joints_max] = {1};

  const u32 jointCount = scene_skeleton_joint_count(skelTempl);
  for (u32 joint = 0; joint != jointCount; ++joint) {
    const StringHash nameHash = scene_skeleton_joint_name(skelTempl, joint);
    const String     name     = stringtable_lookup(g_stringtable, nameHash);

    ui_table_next_row(canvas, table);
    ui_table_draw_row_bg(canvas, table, ui_color(96, 96, 96, 192));

    const u32 parent = scene_skeleton_joint_parent(skelTempl, joint);
    const u32 depth = depthLookup[joint] = depthLookup[parent] + 1;

    ui_label(canvas, fmt_write_scratch("{}{}", fmt_padding(depth), fmt_text(name)), .fontSize = 12);
    ui_table_next_column(canvas, table);

    const SceneJointPose pose = scene_skeleton_sample_def(skelTempl, joint);
    anim_draw_pose(canvas, table, pose);
  }

  ui_style_pop(canvas);
}

static void anim_panel_options_draw(UiCanvasComp* canvas, DebugAnimationSettingsComp* settings) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(5, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 55);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);
  ui_table_add_column(&table, UiTableColumn_Fixed, 80);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);
  ui_table_add_column(&table, UiTableColumn_Fixed, 80);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);
  ui_table_add_column(&table, UiTableColumn_Fixed, 80);

  ui_table_next_row(canvas, &table);
  ui_layout_move_dir(canvas, Ui_Right, 5, UiBase_Absolute);
  ui_label(canvas, string_lit("Draw:"));
  ui_table_next_column(canvas, &table);

  bool drawSkeleton = (settings->flags & DebugAnimationFlags_DrawSkeleton) != 0;
  if (ui_toggle(canvas, &drawSkeleton)) {
    settings->flags ^= DebugAnimationFlags_DrawSkeleton;
  }
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("[Skeleton]"), .fontSize = 14);
  ui_table_next_column(canvas, &table);

  bool drawJoints = (settings->flags & DebugAnimationFlags_DrawJointTransforms) != 0;
  if (ui_toggle(canvas, &drawJoints)) {
    settings->flags ^= DebugAnimationFlags_DrawJointTransforms;
  }
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("[Joints]"), .fontSize = 14);
  ui_table_next_column(canvas, &table);

  bool drawNames = (settings->flags & DebugAnimationFlags_DrawJointNames) != 0;
  if (ui_toggle(canvas, &drawNames)) {
    settings->flags ^= DebugAnimationFlags_DrawJointNames;
  }
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("[Names]"), .fontSize = 14);

  ui_layout_pop(canvas);
}

static void anim_panel_draw(
    UiCanvasComp*                 canvas,
    DebugAnimationPanelComp*      panelComp,
    DebugAnimationSettingsComp*   settings,
    SceneAnimationComp*           anim,
    const SceneSkeletonTemplComp* skelTempl) {
  const String title = fmt_write_scratch("{} Animation Debug", fmt_ui_shape(Animation));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  anim_panel_options_draw(canvas, settings);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  if (anim && skelTempl) {
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

  ui_layout_container_pop(canvas);
  ui_panel_end(canvas, &panelComp->panel);
}

static DebugAnimationSettingsComp* anim_settings_get_or_create(EcsWorld* world) {
  EcsView*     view = ecs_world_view_t(world, SettingsWriteView);
  EcsIterator* itr  = ecs_view_maybe_at(view, ecs_world_global(world));
  return itr ? ecs_view_write_t(itr, DebugAnimationSettingsComp)
             : ecs_world_add_t(world, ecs_world_global(world), DebugAnimationSettingsComp);
}

ecs_system_define(DebugAnimationUpdatePanelSys) {
  DebugAnimationSettingsComp* settings = anim_settings_get_or_create(world);
  EcsView*                    animView = ecs_world_view_t(world, AnimationView);

  SceneAnimationComp*           anim      = null;
  const SceneSkeletonTemplComp* skelTempl = null;

  /**
   * NOTE: Pick an entity to debug, at the moment we just pick the first animated entity.
   */
  EcsIterator* subjectItr = settings->subject ? ecs_view_maybe_at(animView, settings->subject)
                                              : ecs_view_walk(ecs_view_itr(animView));
  if (subjectItr) {
    settings->subject         = ecs_view_entity(subjectItr);
    anim                      = ecs_view_write_t(subjectItr, SceneAnimationComp);
    const EcsEntityId graphic = ecs_view_read_t(subjectItr, SceneRenderableComp)->graphic;
    if (ecs_view_contains(ecs_world_view_t(world, SkeletonTemplView), graphic)) {
      skelTempl = ecs_utils_read_t(world, SkeletonTemplView, graphic, SceneSkeletonTemplComp);
    }
  } else {
    settings->subject = 0;
  }

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugAnimationPanelComp* panelComp = ecs_view_write_t(itr, DebugAnimationPanelComp);
    UiCanvasComp*            canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    anim_panel_draw(canvas, panelComp, settings, anim, skelTempl);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

static void debug_draw_skeleton(
    DebugShapeComp*               shapes,
    const SceneSkeletonComp*      skeleton,
    const SceneSkeletonTemplComp* skeletonTemplate,
    const GeoMatrix*              jointMatrices) {

  for (u32 i = 1; i != skeleton->jointCount; ++i) {
    const u32       parentIndex = scene_skeleton_joint_parent(skeletonTemplate, i);
    const GeoVector jointPos    = geo_matrix_to_translation(&jointMatrices[i]);
    const GeoVector parentPos   = geo_matrix_to_translation(&jointMatrices[parentIndex]);
    debug_line(shapes, jointPos, parentPos, geo_color_white);
  }
}

static void debug_draw_joint_transforms(
    DebugShapeComp* shapes, const SceneSkeletonComp* skeleton, const GeoMatrix* jointMatrices) {
  static const f32 g_arrowLength = 0.075f;
  static const f32 g_arrowSize   = 0.0075f;

  for (u32 i = 0; i != skeleton->jointCount; ++i) {
    const GeoVector jointPos = geo_matrix_to_translation(&jointMatrices[i]);

    const GeoVector jointRefX = geo_matrix_transform3(&jointMatrices[i], geo_right);
    const GeoVector jointX    = geo_vector_mul(geo_vector_norm(jointRefX), g_arrowLength);

    const GeoVector jointRefY = geo_matrix_transform3(&jointMatrices[i], geo_up);
    const GeoVector jointY    = geo_vector_mul(geo_vector_norm(jointRefY), g_arrowLength);

    const GeoVector jointRefZ = geo_matrix_transform3(&jointMatrices[i], geo_forward);
    const GeoVector jointZ    = geo_vector_mul(geo_vector_norm(jointRefZ), g_arrowLength);

    debug_arrow(shapes, jointPos, geo_vector_add(jointPos, jointX), g_arrowSize, geo_color_red);
    debug_arrow(shapes, jointPos, geo_vector_add(jointPos, jointY), g_arrowSize, geo_color_green);
    debug_arrow(shapes, jointPos, geo_vector_add(jointPos, jointZ), g_arrowSize, geo_color_blue);
  }
}

static void debug_draw_joint_names(
    DebugTextComp*                text,
    const SceneSkeletonComp*      skeleton,
    const SceneSkeletonTemplComp* skeletonTemplate,
    const GeoMatrix*              jointMatrices) {

  for (u32 i = 0; i != skeleton->jointCount; ++i) {
    const GeoVector  jointPos      = geo_matrix_to_translation(&jointMatrices[i]);
    const StringHash jointNameHash = scene_skeleton_joint_name(skeletonTemplate, i);
    const String     jointName     = stringtable_lookup(g_stringtable, jointNameHash);
    debug_text(text, geo_vector_add(jointPos, geo_vector(0, 0.02f, 0)), jointName, geo_color_white);
  }
}

ecs_system_define(DebugAnimationDrawSys) {
  EcsView* globalView   = ecs_world_view_t(world, GlobalDrawView);
  EcsView* animView     = ecs_world_view_t(world, AnimationView);
  EcsView* templateView = ecs_world_view_t(world, SkeletonTemplView);

  EcsIterator* globalItr = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const DebugAnimationSettingsComp* set   = ecs_view_read_t(globalItr, DebugAnimationSettingsComp);
  DebugShapeComp*                   shape = ecs_view_write_t(globalItr, DebugShapeComp);
  DebugTextComp*                    text  = ecs_view_write_t(globalItr, DebugTextComp);

  EcsIterator* itr = set->subject ? ecs_view_maybe_at(animView, set->subject) : null;
  if (!itr) {
    return;
  }
  const EcsEntityId graphic = ecs_view_read_t(itr, SceneRenderableComp)->graphic;
  if (!ecs_view_contains(templateView, graphic)) {
    return;
  }

  const SceneSkeletonComp* skeleton  = ecs_view_read_t(itr, SceneSkeletonComp);
  const GeoVector          pos       = ecs_view_read_t(itr, SceneTransformComp)->position;
  const GeoQuat            rot       = ecs_view_read_t(itr, SceneTransformComp)->rotation;
  const SceneScaleComp*    scaleComp = ecs_view_read_t(itr, SceneScaleComp);
  const f32                scale     = scaleComp ? scaleComp->scale : 1.0f;
  const GeoMatrix          transform = geo_matrix_trs(pos, rot, geo_vector(scale, scale, scale));

  const SceneSkeletonTemplComp* template =
      ecs_utils_read(templateView, graphic, ecs_comp_id(SceneSkeletonTemplComp));

  GeoMatrix* jointMatrices = mem_stack(sizeof(GeoMatrix) * skeleton->jointCount).ptr;
  for (u32 i = 0; i != skeleton->jointCount; ++i) {
    jointMatrices[i] = geo_matrix_mul(&transform, &skeleton->jointTransforms[i]);
  }

  if (set->flags & DebugAnimationFlags_DrawSkeleton) {
    debug_draw_skeleton(shape, skeleton, template, jointMatrices);
  }
  if (set->flags & DebugAnimationFlags_DrawJointTransforms) {
    debug_draw_joint_transforms(shape, skeleton, jointMatrices);
  }
  if (set->flags & DebugAnimationFlags_DrawJointNames) {
    debug_draw_joint_names(text, skeleton, template, jointMatrices);
  }
}

ecs_module_init(debug_animation_module) {
  ecs_register_comp(DebugAnimationSettingsComp);
  ecs_register_comp(DebugAnimationPanelComp);

  ecs_register_view(SettingsWriteView);
  ecs_register_view(PanelUpdateView);
  ecs_register_view(AnimationView);
  ecs_register_view(SkeletonTemplView);
  ecs_register_view(GlobalDrawView);

  ecs_register_system(
      DebugAnimationUpdatePanelSys,
      ecs_view_id(SettingsWriteView),
      ecs_view_id(PanelUpdateView),
      ecs_view_id(AnimationView),
      ecs_view_id(SkeletonTemplView));

  ecs_register_system(
      DebugAnimationDrawSys,
      ecs_view_id(GlobalDrawView),
      ecs_view_id(AnimationView),
      ecs_view_id(SkeletonTemplView));

  ecs_order(DebugAnimationDrawSys, DebugOrder_AnimationDebugDraw);
}

EcsEntityId debug_animation_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, panelEntity, DebugAnimationPanelComp, .panel = ui_panel(ui_vector(875, 350)));
  return panelEntity;
}
