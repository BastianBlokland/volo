#include "debug_physics.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "ecs_world.h"
#include "scene_bounds.h"
#include "scene_camera.h"
#include "scene_skeleton.h"
#include "scene_transform.h"
#include "ui.h"

typedef enum {
  DebugPhysicsFlags_DrawPivot        = 1 << 0,
  DebugPhysicsFlags_DrawOrientation  = 1 << 1,
  DebugPhysicsFlags_DrawBoundsLocal  = 1 << 2,
  DebugPhysicsFlags_DrawBoundsGlobal = 1 << 3,
  DebugPhysicsFlags_DrawSkeleton     = 1 << 4,

  DebugPhysicsFlags_DrawAny = DebugPhysicsFlags_DrawPivot | DebugPhysicsFlags_DrawOrientation |
                              DebugPhysicsFlags_DrawBoundsLocal |
                              DebugPhysicsFlags_DrawBoundsGlobal | DebugPhysicsFlags_DrawSkeleton
} DebugPhysicsFlags;

ecs_comp_define(DebugPhysicsSettingsComp) { DebugPhysicsFlags flags; };

ecs_comp_define(DebugPhysicsPanelComp) { UiPanel panel; };

ecs_view_define(SettingsUpdateView) { ecs_access_write(DebugPhysicsSettingsComp); }

ecs_view_define(GlobalDrawView) {
  ecs_access_read(DebugPhysicsSettingsComp);
  ecs_access_write(DebugShapeComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugPhysicsPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(ObjectView) {
  ecs_access_maybe_read(SceneBoundsComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneSkeletonComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_without(SceneCameraComp);
}

static void physics_panel_draw(
    UiCanvasComp* canvas, DebugPhysicsPanelComp* panelComp, DebugPhysicsSettingsComp* settings) {
  const String title = fmt_write_scratch("{} Physics Debug", fmt_ui_shape(ViewInAr));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 175);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Draw pivot"));
  ui_table_next_column(canvas, &table);
  bool drawPivot = (settings->flags & DebugPhysicsFlags_DrawPivot) != 0;
  if (ui_toggle(canvas, &drawPivot)) {
    settings->flags ^= DebugPhysicsFlags_DrawPivot;
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Draw orientation"));
  ui_table_next_column(canvas, &table);
  bool drawOrientation = (settings->flags & DebugPhysicsFlags_DrawOrientation) != 0;
  if (ui_toggle(canvas, &drawOrientation)) {
    settings->flags ^= DebugPhysicsFlags_DrawOrientation;
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Draw bounds local"));
  ui_table_next_column(canvas, &table);
  bool drawBoundsLocal = (settings->flags & DebugPhysicsFlags_DrawBoundsLocal) != 0;
  if (ui_toggle(canvas, &drawBoundsLocal)) {
    settings->flags ^= DebugPhysicsFlags_DrawBoundsLocal;
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Draw bounds global"));
  ui_table_next_column(canvas, &table);
  bool drawBoundsGlobal = (settings->flags & DebugPhysicsFlags_DrawBoundsGlobal) != 0;
  if (ui_toggle(canvas, &drawBoundsGlobal)) {
    settings->flags ^= DebugPhysicsFlags_DrawBoundsGlobal;
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Draw skeleton"));
  ui_table_next_column(canvas, &table);
  bool drawSkeleton = (settings->flags & DebugPhysicsFlags_DrawSkeleton) != 0;
  if (ui_toggle(canvas, &drawSkeleton)) {
    settings->flags ^= DebugPhysicsFlags_DrawSkeleton;
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugPhysicsUpdatePanelSys) {
  EcsView*                  settingsView = ecs_world_view_t(world, SettingsUpdateView);
  EcsIterator*              settingsItr  = ecs_view_maybe_at(settingsView, ecs_world_global(world));
  DebugPhysicsSettingsComp* settings =
      settingsItr ? ecs_view_write_t(settingsItr, DebugPhysicsSettingsComp)
                  : ecs_world_add_t(world, ecs_world_global(world), DebugPhysicsSettingsComp);

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId      entity    = ecs_view_entity(itr);
    DebugPhysicsPanelComp* panelComp = ecs_view_write_t(itr, DebugPhysicsPanelComp);
    UiCanvasComp*          canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    physics_panel_draw(canvas, panelComp, settings);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

static void physics_draw_bounds_local(
    DebugShapeComp* shape,
    const GeoVector pos,
    const GeoQuat   rot,
    const GeoBox    bounds,
    const f32       scale) {
  const GeoVector size = geo_vector_mul(geo_box_size(&bounds), scale);
  const GeoVector center =
      geo_vector_add(geo_quat_rotate(rot, geo_vector_mul(geo_box_center(&bounds), scale)), pos);
  debug_box(shape, center, rot, size, geo_color(0, 1, 0, 0.2f), DebugShape_Fill);
  debug_box(shape, center, rot, size, geo_color(0, 1, 0, 0.5f), DebugShape_Wire);
}

static void physics_draw_bounds_global(
    DebugShapeComp* canvas,
    const GeoVector pos,
    const GeoQuat   rot,
    const GeoBox    bounds,
    const f32       scale) {
  const GeoBox    aabb   = geo_box_transform3(&bounds, pos, rot, scale);
  const GeoVector center = geo_box_center(&aabb);
  const GeoVector size   = geo_box_size(&aabb);
  debug_box(canvas, center, geo_quat_ident, size, geo_color(0, 0, 1, 0.2f), DebugShape_Fill);
  debug_box(canvas, center, geo_quat_ident, size, geo_color(0, 0, 1, 0.5f), DebugShape_Wire);
}

static void physics_draw_skeleton(
    DebugShapeComp*          canvas,
    const SceneSkeletonComp* skeleton,
    const GeoVector          pos,
    const GeoQuat            rot,
    const f32                scale) {
  static const f32 g_arrowLength = 0.1f;
  static const f32 g_arrowSize   = 0.01f;
  const f32        jointScaleMul = (1.0f / scale) * g_arrowLength;

  const GeoMatrix transform = geo_matrix_trs(pos, rot, geo_vector(scale, scale, scale));
  for (u32 i = 0; i != skeleton->jointCount; ++i) {
    const GeoMatrix jointTransform = geo_matrix_mul(&transform, &skeleton->jointTransforms[i]);
    const GeoVector jointPos       = geo_matrix_to_translation(&jointTransform);

    const GeoVector jointRefX = geo_matrix_transform3_point(&jointTransform, geo_right);
    const GeoVector jointX    = geo_vector_mul(geo_vector_sub(jointRefX, jointPos), jointScaleMul);

    const GeoVector jointRefY = geo_matrix_transform3_point(&jointTransform, geo_up);
    const GeoVector jointY    = geo_vector_mul(geo_vector_sub(jointRefY, jointPos), jointScaleMul);

    const GeoVector jointRefZ = geo_matrix_transform3_point(&jointTransform, geo_forward);
    const GeoVector jointZ    = geo_vector_mul(geo_vector_sub(jointRefZ, jointPos), jointScaleMul);

    debug_arrow(canvas, jointPos, geo_vector_add(jointPos, jointX), g_arrowSize, geo_color_red);
    debug_arrow(canvas, jointPos, geo_vector_add(jointPos, jointY), g_arrowSize, geo_color_green);
    debug_arrow(canvas, jointPos, geo_vector_add(jointPos, jointZ), g_arrowSize, geo_color_blue);
  }
}

ecs_system_define(DebugPhysicsDrawSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalDrawView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const DebugPhysicsSettingsComp* settings = ecs_view_read_t(globalItr, DebugPhysicsSettingsComp);
  if (!(settings->flags & DebugPhysicsFlags_DrawAny)) {
    return;
  }

  DebugShapeComp* shape      = ecs_view_write_t(globalItr, DebugShapeComp);
  EcsView*        objectView = ecs_world_view_t(world, ObjectView);
  for (EcsIterator* itr = ecs_view_itr(objectView); ecs_view_walk(itr);) {
    const GeoVector          pos          = ecs_view_read_t(itr, SceneTransformComp)->position;
    const GeoQuat            rot          = ecs_view_read_t(itr, SceneTransformComp)->rotation;
    const SceneBoundsComp*   boundsComp   = ecs_view_read_t(itr, SceneBoundsComp);
    const SceneSkeletonComp* skeletonComp = ecs_view_read_t(itr, SceneSkeletonComp);
    const SceneScaleComp*    scaleComp    = ecs_view_read_t(itr, SceneScaleComp);
    const f32                scale        = scaleComp ? scaleComp->scale : 1.0f;

    if (settings->flags & DebugPhysicsFlags_DrawPivot) {
      debug_sphere(shape, pos, 0.025f, geo_color(1.0f, 1.0f, 0.0f, 1.0f), DebugShape_Overlay);
    }
    if (settings->flags & DebugPhysicsFlags_DrawOrientation) {
      debug_orientation(shape, pos, rot, 0.25f);
    }
    if (boundsComp && !geo_box_is_inverted3(&boundsComp->local)) {
      if (settings->flags & DebugPhysicsFlags_DrawBoundsLocal) {
        physics_draw_bounds_local(shape, pos, rot, boundsComp->local, scale);
      }
      if (settings->flags & DebugPhysicsFlags_DrawBoundsGlobal) {
        physics_draw_bounds_global(shape, pos, rot, boundsComp->local, scale);
      }
    }
    if (skeletonComp && settings->flags & DebugPhysicsFlags_DrawSkeleton) {
      physics_draw_skeleton(shape, skeletonComp, pos, rot, scale);
    }
  }
}

ecs_module_init(debug_physics_module) {
  ecs_register_comp(DebugPhysicsSettingsComp);
  ecs_register_comp(DebugPhysicsPanelComp);

  ecs_register_view(SettingsUpdateView);
  ecs_register_view(GlobalDrawView);
  ecs_register_view(PanelUpdateView);
  ecs_register_view(ObjectView);

  ecs_register_system(
      DebugPhysicsUpdatePanelSys, ecs_view_id(SettingsUpdateView), ecs_view_id(PanelUpdateView));

  ecs_register_system(DebugPhysicsDrawSys, ecs_view_id(GlobalDrawView), ecs_view_id(ObjectView));

  ecs_order(DebugPhysicsDrawSys, DebugOrder_PhysicsDebugDraw);
}

EcsEntityId debug_physics_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, panelEntity, DebugPhysicsPanelComp, .panel = ui_panel(ui_vector(330, 255)));
  return panelEntity;
}
