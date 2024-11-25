#include "core_array.h"
#include "core_bits.h"
#include "core_format.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "debug_skeleton.h"
#include "debug_text.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_renderable.h"
#include "scene_set.h"
#include "scene_skeleton.h"
#include "scene_transform.h"
#include "ui.h"

typedef enum {
  DebugSkelFlags_DrawSkeleton        = 1 << 0,
  DebugSkelFlags_DrawJointTransforms = 1 << 1,
  DebugSkelFlags_DrawJointNames      = 1 << 2,
  DebugSkelFlags_DrawSkinCounts      = 1 << 3,
  DebugSkelFlags_DrawBounds          = 1 << 4,
  DebugSkelFlags_DrawAny             = bit_range_32(0, 5),

  DebugSkelFlags_Default = 0,
} DebugSkelFlags;

static const String g_skeletonFlagNames[] = {
    string_static("Skeleton"),
    string_static("Transforms"),
    string_static("Names"),
    string_static("Skin counts"),
    string_static("Bounds"),
};

ecs_comp_define(DebugSkelSettingsComp) { DebugSkelFlags flags; };

ecs_comp_define(DebugSkelPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
  u32          totalRows;
};

ecs_view_define(SettingsWriteView) { ecs_access_write(DebugSkelSettingsComp); }

ecs_view_define(SubjectView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_read(SceneSkeletonComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_write(SceneAnimationComp);
}

ecs_view_define(SkeletonTemplView) { ecs_access_read(SceneSkeletonTemplComp); }

typedef struct {
  bool                          valid;
  f32                           worldScale;
  GeoMatrix                     worldMat;
  SceneAnimationComp*           animation;
  const SceneSkeletonComp*      skeleton;
  const SceneSkeletonTemplComp* skeletonTemplate;
} DebugSkelSubject;

static DebugSkelSubject debug_skel_subject(EcsWorld* world, const EcsEntityId entity) {
  EcsView* subjectView   = ecs_world_view_t(world, SubjectView);
  EcsView* skelTemplView = ecs_world_view_t(world, SkeletonTemplView);

  EcsIterator* subjectItr = ecs_view_maybe_at(subjectView, entity);
  if (!subjectItr) {
    return (DebugSkelSubject){0};
  }
  const EcsEntityId graphic      = ecs_view_read_t(subjectItr, SceneRenderableComp)->graphic;
  EcsIterator*      skelTemplItr = ecs_view_maybe_at(skelTemplView, graphic);
  if (!skelTemplItr) {
    return (DebugSkelSubject){0};
  }
  const SceneTransformComp* transComp = ecs_view_read_t(subjectItr, SceneTransformComp);
  const SceneScaleComp*     scaleComp = ecs_view_read_t(subjectItr, SceneScaleComp);
  return (DebugSkelSubject){
      .valid            = true,
      .worldScale       = scaleComp ? scaleComp->scale : 1.0f,
      .worldMat         = scene_matrix_world(transComp, scaleComp),
      .animation        = ecs_view_write_t(subjectItr, SceneAnimationComp),
      .skeleton         = ecs_view_read_t(subjectItr, SceneSkeletonComp),
      .skeletonTemplate = ecs_view_read_t(skelTemplItr, SceneSkeletonTemplComp),
  };
}

static void
skel_draw_vec(UiCanvasComp* canvas, const GeoVector v, const u8 digits, const String tooltip) {
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

static void skel_draw_quat(UiCanvasComp* canvas, const GeoQuat q, const String tooltip) {
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

static void skel_draw_pose(UiCanvasComp* canvas, UiTable* table, const SceneJointPose pose) {
  skel_draw_vec(canvas, pose.t, 1, string_lit("Translation."));
  ui_table_next_column(canvas, table);
  skel_draw_quat(canvas, pose.r, string_lit("Rotation."));
  ui_table_next_column(canvas, table);
  skel_draw_vec(canvas, pose.s, 2, string_lit("Scale."));
  ui_table_next_column(canvas, table);
}

static void skel_draw_pose_animated(
    UiCanvasComp* canvas, UiTable* table, const SceneJointPose pose, const SceneJointInfo info) {
  ui_style_push(canvas);
  if (info.frameCountT) {
    const u32 count = info.frameCountT;
    ui_style_color(canvas, count > 1 ? ui_color_yellow : ui_color_white);
    skel_draw_vec(
        canvas, pose.t, 1, fmt_write_scratch("Translation.\nFrames: {}.", fmt_int(count)));
  }
  ui_table_next_column(canvas, table);
  if (info.frameCountR) {
    const u32 count = info.frameCountR;
    ui_style_color(canvas, count > 1 ? ui_color_yellow : ui_color_white);
    skel_draw_quat(canvas, pose.r, fmt_write_scratch("Rotation.\nFrames: {}.", fmt_int(count)));
  }
  ui_table_next_column(canvas, table);
  if (info.frameCountS) {
    const u32 count = info.frameCountS;
    ui_style_color(canvas, count > 1 ? ui_color_yellow : ui_color_white);
    skel_draw_vec(canvas, pose.s, 2, fmt_write_scratch("Scale.\nFrames: {}.", fmt_int(count)));
  }
  ui_style_pop(canvas);
}

static void skel_draw_joints_layer(
    UiCanvasComp*                 canvas,
    UiTable*                      table,
    const SceneAnimLayer*         layer,
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

    const u32 parent = scene_skeleton_joint_parent(skelTempl, joint);
    const u32 depth = depthLookup[joint] = depthLookup[parent] + 1;
    ui_label(canvas, fmt_write_scratch("{}{}", fmt_padding(depth), fmt_text(name)), .fontSize = 12);
    ui_table_next_column(canvas, table);

    const SceneJointPose pose = scene_skeleton_sample(skelTempl, layerIdx, joint, layer->time);
    skel_draw_pose_animated(canvas, table, pose, info);
    ui_table_next_column(canvas, table);

    const f32 maskWeight = scene_skeleton_mask(skelTempl, layerIdx, joint);
    ui_label(
        canvas,
        fmt_write_scratch("{}", fmt_float(maskWeight, .minDecDigits = 2, .maxDecDigits = 2)),
        .fontSize = 12,
        .tooltip  = string_lit("Mask weight."));
    ui_table_next_column(canvas, table);
  }

  ui_style_pop(canvas);
}

static void skel_draw_joints_def(
    UiCanvasComp* canvas, UiTable* table, const SceneSkeletonTemplComp* skelTempl) {

  ui_style_push(canvas);
  ui_style_variation(canvas, UiVariation_Monospace);

  ui_table_next_row(canvas, table);
  ui_table_draw_row_bg(canvas, table, ui_color(96, 96, 96, 192));
  ui_label(canvas, string_lit("<root>"), .fontSize = 12);
  ui_table_next_column(canvas, table);

  const SceneJointPose rootPose = scene_skeleton_root(skelTempl);
  skel_draw_pose(canvas, table, rootPose);

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
    skel_draw_pose(canvas, table, pose);
  }

  ui_style_pop(canvas);
}

static void skel_panel_drag_flags(UiCanvasComp* canvas, SceneAnimLayer* layer) {
  static const struct {
    SceneAnimFlags flag;
    String         label;
    String         tooltip;
  } g_flagMeta[] = {
      {
          .flag    = SceneAnimFlags_Loop,
          .label   = string_static("L"),
          .tooltip = string_static("Looping playback"),
      },
      {
          .flag    = SceneAnimFlags_AutoFadeIn,
          .label   = string_static("I"),
          .tooltip = string_static("Automatic fade-in over the first 25% of the playback"),
      },
      {
          .flag    = SceneAnimFlags_AutoFadeOut,
          .label   = string_static("O"),
          .tooltip = string_static("Automatic fade-out over the last 25% of the playback"),
      },
  };
  static const UiColor g_colorActive   = {0, 128, 0, 192};
  static const UiColor g_colorInactive = {32, 32, 32, 192};

  ui_layout_resize(canvas, UiAlign_BottomLeft, ui_vector(25, 0), UiBase_Absolute, Ui_X);
  for (u32 i = 0; i != array_elems(g_flagMeta); ++i) {
    if (ui_button(
            canvas,
            .label      = g_flagMeta[i].label,
            .fontSize   = 14,
            .tooltip    = g_flagMeta[i].tooltip,
            .frameColor = layer->flags & g_flagMeta[i].flag ? g_colorActive : g_colorInactive)) {
      layer->flags ^= g_flagMeta[i].flag;
    }
    ui_layout_next(canvas, Ui_Right, 5);
  }
}

static void skel_panel_options_draw(UiCanvasComp* canvas, DebugSkelSettingsComp* settings) {
  ui_layout_push(canvas);

  static const DebugSkelFlags g_drawAny = DebugSkelFlags_DrawAny;

  UiTable table = ui_table(.spacing = ui_vector(5, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  bitset_for(bitset_from_var(g_drawAny), i) {
    ui_table_add_column(&table, UiTableColumn_Fixed, 25);
    ui_table_add_column(&table, UiTableColumn_Fixed, 125);
  }

  ui_table_next_row(canvas, &table);
  ui_layout_move_dir(canvas, Ui_Right, 5, UiBase_Absolute);
  ui_label(canvas, string_lit("Draw:"));
  ui_table_next_column(canvas, &table);

  bitset_for(bitset_from_var(g_drawAny), i) {
    ui_toggle_flag(canvas, (u32*)&settings->flags, 1 << i);
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("[{}]", fmt_text(g_skeletonFlagNames[i])), .fontSize = 14);
    ui_table_next_column(canvas, &table);
  }

  ui_layout_pop(canvas);
}

static void skel_panel_draw(
    UiCanvasComp*          canvas,
    DebugSkelPanelComp*    panelComp,
    DebugSkelSettingsComp* settings,
    const DebugSkelSubject subject) {
  const String title = fmt_write_scratch("{} Skeleton Panel", fmt_ui_shape(Body));
  ui_panel_begin(
      canvas, &panelComp->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  skel_panel_options_draw(canvas, settings);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  if (subject.valid) {
    UiTable table = ui_table(.spacing = ui_vector(10, 5));
    ui_table_add_column(&table, UiTableColumn_Fixed, 300);
    ui_table_add_column(&table, UiTableColumn_Fixed, 140);
    ui_table_add_column(&table, UiTableColumn_Fixed, 150);
    ui_table_add_column(&table, UiTableColumn_Fixed, 140);
    ui_table_add_column(&table, UiTableColumn_Fixed, 60);
    ui_table_add_column(&table, UiTableColumn_Flexible, 0);

    ui_table_draw_header(
        canvas,
        &table,
        (const UiTableColumnName[]){
            {string_lit("Animation"), string_lit("Animation name.")},
            {string_lit("Time"), string_lit("Playback time.")},
            {string_lit("Progress"), string_lit("Playback progress.")},
            {string_lit("Speed"), string_lit("Playback speed.")},
            {string_lit("Weight"), string_lit("Playback weight.")},
            {string_lit("Flags"), string_lit("Playback flags.")},
        });

    const f32 totalHeight = ui_table_height(&table, panelComp->totalRows);
    ui_scrollview_begin(canvas, &panelComp->scrollview, totalHeight);
    panelComp->totalRows = 1; // Always draws the default layer.

    for (u32 layerIdx = 0; layerIdx != subject.animation->layerCount; ++layerIdx) {
      SceneAnimLayer* layer = &subject.animation->layers[layerIdx];
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

      ui_slider(canvas, &layer->speed, .min = -2.5f, .max = 2.5f);
      ui_table_next_column(canvas, &table);

      ui_slider(canvas, &layer->weight);
      ui_table_next_column(canvas, &table);

      skel_panel_drag_flags(canvas, layer);
      ui_table_next_column(canvas, &table);

      if (open) {
        skel_draw_joints_layer(canvas, &table, layer, layerIdx, subject.skeletonTemplate);
      }

      panelComp->totalRows += 1 + (open ? scene_skeleton_joint_count(subject.skeletonTemplate) : 0);

      ui_canvas_id_block_next(canvas); // Use a consistent amount of ids regardless of if 'open'.
    }

    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));
    if (ui_section(canvas, .label = string_lit("<default>"))) {
      skel_draw_joints_def(canvas, &table, subject.skeletonTemplate);
      panelComp->totalRows += scene_skeleton_joint_count(subject.skeletonTemplate) + 1;
    }

    ui_scrollview_end(canvas, &panelComp->scrollview);
  } else {
    ui_label(
        canvas, string_lit("Select an entity with a skeleton."), .align = UiAlign_MiddleCenter);
  }

  ui_layout_container_pop(canvas);
  ui_panel_end(canvas, &panelComp->panel);
}

static DebugSkelSettingsComp* skel_settings_get_or_create(EcsWorld* world) {
  EcsView*     view = ecs_world_view_t(world, SettingsWriteView);
  EcsIterator* itr  = ecs_view_maybe_at(view, ecs_world_global(world));
  if (itr) {
    return ecs_view_write_t(itr, DebugSkelSettingsComp);
  }
  return ecs_world_add_t(
      world, ecs_world_global(world), DebugSkelSettingsComp, .flags = DebugSkelFlags_Default);
}

ecs_view_define(PanelUpdateGlobalView) { ecs_access_read(SceneSetEnvComp); }

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DebugSkelPanelComp's are exclusively managed here.

  ecs_access_read(DebugPanelComp);
  ecs_access_write(DebugSkelPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DebugSkeletonUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  DebugSkelSettingsComp* settings = skel_settings_get_or_create(world);

  const SceneSetEnvComp* setEnv      = ecs_view_read_t(globalItr, SceneSetEnvComp);
  const StringHash       selectedSet = g_sceneSetSelected;
  const DebugSkelSubject subject = debug_skel_subject(world, scene_set_main(setEnv, selectedSet));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugSkelPanelComp* panelComp = ecs_view_write_t(itr, DebugSkelPanelComp);
    UiCanvasComp*       canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (debug_panel_hidden(ecs_view_read_t(itr, DebugPanelComp)) && !pinned) {
      continue;
    }
    skel_panel_draw(canvas, panelComp, settings, subject);

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

static void debug_draw_skeleton(
    DebugShapeComp*               shape,
    const SceneSkeletonTemplComp* skeletonTemplate,
    const u32                     jointCount,
    const GeoMatrix*              jointMatrices) {

  for (u32 i = 1; i != jointCount; ++i) {
    const u32       parentIndex = scene_skeleton_joint_parent(skeletonTemplate, i);
    const GeoVector jointPos    = geo_matrix_to_translation(&jointMatrices[i]);
    const GeoVector parentPos   = geo_matrix_to_translation(&jointMatrices[parentIndex]);
    debug_line(shape, jointPos, parentPos, geo_color_purple);
  }
}

static void debug_draw_joint_transforms(
    DebugShapeComp* shapes, const u32 jointCount, const GeoMatrix* jointMatrices) {
  static const f32 g_arrowLength = 0.075f;
  static const f32 g_arrowSize   = 0.0075f;

  for (u32 i = 0; i != jointCount; ++i) {
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
    const SceneSkeletonTemplComp* skeletonTemplate,
    const u32                     jointCount,
    const GeoMatrix*              jointMatrices) {

  for (u32 i = 0; i != jointCount; ++i) {
    const GeoVector  jointPos  = geo_matrix_to_translation(&jointMatrices[i]);
    const StringHash jointName = scene_skeleton_joint_name(skeletonTemplate, i);
    debug_text(text, jointPos, stringtable_lookup(g_stringtable, jointName));
  }
}

static void debug_draw_skin_counts(
    DebugTextComp*                text,
    const SceneSkeletonTemplComp* skeletonTemplate,
    const u32                     jointCount,
    const GeoMatrix*              jointMatrices) {

  for (u32 i = 0; i != jointCount; ++i) {
    const GeoVector jointPos  = geo_matrix_to_translation(&jointMatrices[i]);
    const u32       skinCount = scene_skeleton_joint_skin_count(skeletonTemplate, i);
    const GeoColor  color     = skinCount ? geo_color_white : geo_color_red;
    debug_text(text, jointPos, fmt_write_scratch("{}", fmt_int(skinCount)), .color = color);
  }
}

static void debug_draw_bounds(
    DebugShapeComp*               shape,
    const SceneSkeletonTemplComp* skeletonTemplate,
    const f32                     worldScale,
    const u32                     jointCount,
    const GeoMatrix*              jointMatrices) {

  for (u32 i = 0; i != jointCount; ++i) {
    const GeoVector jointPos = geo_matrix_to_translation(&jointMatrices[i]);

    const f32 radius       = scene_skeleton_joint_bounding_radius(skeletonTemplate, i);
    const f32 radiusScaled = radius * worldScale;

    debug_sphere(shape, jointPos, radiusScaled, geo_color(0, 1, 0, 0.1f), DebugShape_Fill);
    debug_sphere(shape, jointPos, radiusScaled, geo_color(0, 1, 0, 0.5f), DebugShape_Wire);
  }
}

ecs_view_define(GlobalDrawView) {
  ecs_access_read(DebugSkelSettingsComp);
  ecs_access_read(SceneSetEnvComp);
  ecs_access_write(DebugShapeComp);
  ecs_access_write(DebugTextComp);
}

ecs_system_define(DebugSkeletonDrawSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalDrawView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneSetEnvComp*       setEnv = ecs_view_read_t(globalItr, SceneSetEnvComp);
  const DebugSkelSettingsComp* set    = ecs_view_read_t(globalItr, DebugSkelSettingsComp);
  DebugShapeComp*              shape  = ecs_view_write_t(globalItr, DebugShapeComp);
  DebugTextComp*               text   = ecs_view_write_t(globalItr, DebugTextComp);

  if (!(set->flags & DebugSkelFlags_DrawAny)) {
    return; // Nothing requested to be drawn.
  }

  GeoMatrix jointMatrices[scene_skeleton_joints_max];

  const StringHash s = g_sceneSetSelected;
  for (const EcsEntityId* e = scene_set_begin(setEnv, s); e != scene_set_end(setEnv, s); ++e) {
    const DebugSkelSubject subject = debug_skel_subject(world, *e);
    if (!subject.valid) {
      continue;
    }

    for (u32 i = 0; i != subject.skeleton->jointCount; ++i) {
      jointMatrices[i] = geo_matrix_mul(&subject.worldMat, &subject.skeleton->jointTransforms[i]);
    }

    if (set->flags & DebugSkelFlags_DrawSkeleton) {
      debug_draw_skeleton(
          shape, subject.skeletonTemplate, subject.skeleton->jointCount, jointMatrices);
    }
    if (set->flags & DebugSkelFlags_DrawJointTransforms) {
      debug_draw_joint_transforms(shape, subject.skeleton->jointCount, jointMatrices);
    }
    if (set->flags & DebugSkelFlags_DrawJointNames) {
      debug_draw_joint_names(
          text, subject.skeletonTemplate, subject.skeleton->jointCount, jointMatrices);
    }
    if (set->flags & DebugSkelFlags_DrawSkinCounts) {
      debug_draw_skin_counts(
          text, subject.skeletonTemplate, subject.skeleton->jointCount, jointMatrices);
    }
    if (set->flags & DebugSkelFlags_DrawBounds) {
      debug_draw_bounds(
          shape,
          subject.skeletonTemplate,
          subject.worldScale,
          subject.skeleton->jointCount,
          jointMatrices);
    }
  }
}

ecs_module_init(debug_skeleton_module) {
  ecs_register_comp(DebugSkelSettingsComp);
  ecs_register_comp(DebugSkelPanelComp);

  ecs_register_view(SettingsWriteView);
  ecs_register_view(PanelUpdateGlobalView);
  ecs_register_view(PanelUpdateView);
  ecs_register_view(SubjectView);
  ecs_register_view(SkeletonTemplView);
  ecs_register_view(GlobalDrawView);

  ecs_register_system(
      DebugSkeletonUpdatePanelSys,
      ecs_view_id(SettingsWriteView),
      ecs_view_id(PanelUpdateGlobalView),
      ecs_view_id(PanelUpdateView),
      ecs_view_id(SubjectView),
      ecs_view_id(SkeletonTemplView));

  ecs_register_system(
      DebugSkeletonDrawSys,
      ecs_view_id(GlobalDrawView),
      ecs_view_id(SubjectView),
      ecs_view_id(SkeletonTemplView));

  ecs_order(DebugSkeletonDrawSys, DebugOrder_SkeletonDebugDraw);
}

EcsEntityId
debug_skeleton_panel_open(EcsWorld* world, const EcsEntityId window, const DebugPanelType type) {
  const EcsEntityId   panelEntity   = debug_panel_create(world, window, type);
  DebugSkelPanelComp* skeletonPanel = ecs_world_add_t(
      world, panelEntity, DebugSkelPanelComp, .panel = ui_panel(.size = ui_vector(950, 350)));

  if (type == DebugPanelType_Detached) {
    ui_panel_maximize(&skeletonPanel->panel);
  }

  return panelEntity;
}
