#include "core_array.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "debug_camera.h"
#include "debug_gizmo.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "debug_text.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_name.h"
#include "scene_tag.h"
#include "scene_transform.h"
#include "ui.h"

// clang-format off

static const String g_tooltipOrthoSize      = string_static("Size (in meters) of the dominant dimension of the orthographic projection.");
static const String g_tooltipFov            = string_static("Field of view of the dominant dimension of the perspective projection.");
static const String g_tooltipVerticalAspect = string_static("Use the vertical dimension as the dominant dimension.");
static const String g_tooltipDebugFrustum   = string_static("Visualize the camera frustum.");
static const String g_tooltipDebugInput     = string_static("Visualize the input ray.");
static const String g_tooltipNearDistance   = string_static("Distance (in meters) to the near clipping plane.");
static const String g_tooltipExclude        = string_static("Exclude {} from being rendered.");
static const String g_tooltipDefaults       = string_static("Reset all settings to their defaults.");

// clang-format on

static const String g_projectionNames[] = {
    string_static("Perspective"),
    string_static("Orthographic"),
};

ecs_comp_define(DebugCameraPanelComp) {
  UiPanel     panel;
  EcsEntityId window;
};

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugCameraPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(WindowView) {
  ecs_access_write(SceneCameraComp);
  ecs_access_maybe_write(SceneTransformComp);
}

static void camera_default_transform(const SceneCameraComp* camera, SceneTransformComp* transform) {
  if (camera->flags & SceneCameraFlags_Orthographic) {
    transform->position = geo_vector(0);
    transform->rotation = geo_quat_look(geo_down, geo_forward);
  } else {
    transform->position = geo_vector(0, 1.5f, -3.0f);
    transform->rotation = geo_quat_angle_axis(geo_right, 10 * math_deg_to_rad);
  }
}

static void camera_panel_draw_ortho(
    UiCanvasComp* canvas, UiTable* table, SceneCameraComp* camera, SceneTransformComp* transform) {

  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Size"));
  ui_table_next_column(canvas, table);
  ui_slider(canvas, &camera->orthoSize, .min = 1, .max = 100, .tooltip = g_tooltipOrthoSize);

  if (transform) {
    ui_table_next_row(canvas, table);
    if (ui_button(canvas, .label = string_lit("Top"))) {
      transform->position = geo_vector(0);
      transform->rotation = geo_quat_look(geo_down, geo_forward);
    }
    ui_table_next_column(canvas, table);
    if (ui_button(canvas, .label = string_lit("Front"))) {
      transform->position = geo_vector(0);
      transform->rotation = geo_quat_look(geo_forward, geo_up);
    }
  }
}

static void camera_panel_draw_pers(UiCanvasComp* canvas, UiTable* table, SceneCameraComp* camera) {
  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Field of view"));
  ui_table_next_column(canvas, table);
  f32 fovDegrees = camera->persFov * math_rad_to_deg;
  if (ui_slider(canvas, &fovDegrees, .min = 10, .max = 150, .tooltip = g_tooltipFov)) {
    camera->persFov = fovDegrees * math_deg_to_rad;
  }

  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Near distance"));
  ui_table_next_column(canvas, table);
  ui_slider(canvas, &camera->persNear, .min = 0.001f, .max = 5, .tooltip = g_tooltipNearDistance);
}

static void
camera_panel_draw_filters(UiCanvasComp* canvas, UiTable* table, SceneCameraComp* camera) {
  static const struct {
    SceneTags tag;
    String    name;
  } g_filters[] = {
      {SceneTags_Geometry, string_static("geometry")},
      {SceneTags_Debug, string_static("debug")},
  };

  for (usize i = 0; i != array_elems(g_filters); ++i) {
    const String name = g_filters[i].name;
    const String tooltip =
        format_write_formatted_scratch(g_tooltipExclude, fmt_args(fmt_text(name)));

    ui_table_next_row(canvas, table);
    ui_label(canvas, fmt_write_scratch("Exclude {}", fmt_text(name)));
    ui_table_next_column(canvas, table);
    ui_toggle_flag(canvas, (u32*)&camera->filter.illegal, g_filters[i].tag, .tooltip = tooltip);
  }
}

static void camera_panel_draw(
    UiCanvasComp*         canvas,
    DebugCameraPanelComp* panelComp,
    SceneCameraComp*      camera,
    SceneTransformComp*   transform) {
  const String title = fmt_write_scratch("{} Camera Panel", fmt_ui_shape(PhotoCamera));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  u32* flags = (u32*)&camera->flags;

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 160);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Projection"));
  ui_table_next_column(canvas, &table);
  i32 projectionIdx = (camera->flags & SceneCameraFlags_Orthographic) != 0;
  if (ui_select(canvas, &projectionIdx, g_projectionNames, 2)) {
    if (projectionIdx == 1) {
      camera->flags |= SceneCameraFlags_Orthographic;
    } else {
      camera->flags &= ~SceneCameraFlags_Orthographic;
    }
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Vertical aspect"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, flags, SceneCameraFlags_Vertical, .tooltip = g_tooltipVerticalAspect);

  if (projectionIdx == 1) {
    camera_panel_draw_ortho(canvas, &table, camera, transform);
  } else {
    camera_panel_draw_pers(canvas, &table, camera);
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Gizmo Translation"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, flags, SceneCameraFlags_DebugGizmoTranslation);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Gizmo Rotation"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, flags, SceneCameraFlags_DebugGizmoRotation);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Debug frustum"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, flags, SceneCameraFlags_DebugFrustum, .tooltip = g_tooltipDebugFrustum);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Debug input ray"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, flags, SceneCameraFlags_DebugInputRay, .tooltip = g_tooltipDebugInput);

  camera_panel_draw_filters(canvas, &table, camera);

  ui_table_next_row(canvas, &table);
  if (ui_button(canvas, .label = string_lit("Defaults"), .tooltip = g_tooltipDefaults)) {
    scene_camera_to_default(camera);
    if (transform) {
      camera_default_transform(camera, transform);
    }
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugCameraUpdatePanelSys) {
  EcsIterator* windowItr = ecs_view_itr(ecs_world_view_t(world, WindowView));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugCameraPanelComp* panelComp = ecs_view_write_t(itr, DebugCameraPanelComp);
    UiCanvasComp*         canvas    = ecs_view_write_t(itr, UiCanvasComp);

    if (!ecs_view_maybe_jump(windowItr, panelComp->window)) {
      continue; // Window has been destroyed, or has no camera.
    }
    SceneCameraComp*    camera    = ecs_view_write_t(windowItr, SceneCameraComp);
    SceneTransformComp* transform = ecs_view_write_t(windowItr, SceneTransformComp);

    ui_canvas_reset(canvas);
    camera_panel_draw(canvas, panelComp, camera, transform);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_view_define(GlobalDrawView) {
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_write(DebugGizmoComp);
  ecs_access_write(DebugShapeComp);
  ecs_access_write(DebugTextComp);
}

ecs_view_define(DrawView) {
  ecs_access_read(GapWindowComp);
  ecs_access_read(SceneCameraComp);
  ecs_access_maybe_write(SceneTransformComp);
}

ecs_view_define(NameView) { ecs_access_read(SceneNameComp); }

static void debug_camera_draw_frustum(
    DebugShapeComp*           shape,
    const SceneCameraComp*    cam,
    const SceneTransformComp* trans,
    const f32                 aspect) {
  const GeoMatrix viewProj = scene_camera_view_proj(cam, trans, aspect);
  const GeoVector camPos   = trans ? trans->position : geo_vector(0);
  const GeoVector camFwd   = trans ? geo_quat_rotate(trans->rotation, geo_forward) : geo_forward;

  debug_frustum(shape, &viewProj, geo_color_white);

  GeoPlane frustumPlanes[4];
  geo_matrix_frustum4(&viewProj, frustumPlanes);

  const GeoVector planeRefPos = geo_vector_add(camPos, geo_vector_mul(camFwd, 5));
  array_for_t(frustumPlanes, GeoPlane, p) {
    const GeoVector pos = geo_plane_closest_point(p, planeRefPos);
    const GeoQuat   rot = geo_quat_look(p->normal, camFwd);
    debug_plane(shape, pos, rot, geo_color(1, 1, 0, 0.25f));
  }
}

static void debug_camera_draw_input_ray(
    DebugShapeComp*              shape,
    DebugTextComp*               text,
    const SceneCollisionEnvComp* collisionEnv,
    EcsView*                     nameView,
    const SceneCameraComp*       cam,
    const SceneTransformComp*    trans,
    const f32                    aspect,
    const GeoVector              inputPos) {
  const GeoRay    ray   = scene_camera_ray(cam, trans, aspect, inputPos);
  const GeoVector start = ray.point;
  const GeoVector end   = geo_vector_add(start, geo_vector_mul(ray.dir, 1e10f));
  debug_line(shape, start, end, geo_color_fuchsia);

  SceneRayHit hit;
  if (scene_query_ray(collisionEnv, &ray, &hit)) {
    debug_sphere(shape, hit.position, 0.04f, geo_color_lime, DebugShape_Overlay);
    const GeoVector lineEnd = geo_vector_add(hit.position, geo_vector_mul(hit.normal, 0.5f));
    debug_arrow(shape, hit.position, lineEnd, 0.04f, geo_color_green);

    EcsIterator* nameItr = ecs_view_itr(nameView);
    if (ecs_view_maybe_jump(nameItr, hit.entity)) {
      const SceneNameComp* nameComp = ecs_view_read_t(nameItr, SceneNameComp);
      const GeoVector      pos      = geo_vector_add(hit.position, geo_vector_mul(geo_up, 0.1f));
      debug_text(text, pos, stringtable_lookup(g_stringtable, nameComp->name), geo_color_white);
    }
  }
}

ecs_system_define(DebugCameraDrawSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalDrawView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneCollisionEnvComp* collisionEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  DebugShapeComp*              shape        = ecs_view_write_t(globalItr, DebugShapeComp);
  DebugTextComp*               text         = ecs_view_write_t(globalItr, DebugTextComp);
  DebugGizmoComp*              gizmo        = ecs_view_write_t(globalItr, DebugGizmoComp);

  EcsView* nameView = ecs_world_view_t(world, NameView);
  EcsView* drawView = ecs_world_view_t(world, DrawView);

  for (EcsIterator* itr = ecs_view_itr(drawView); ecs_view_walk(itr);) {
    const SceneCameraComp* cam   = ecs_view_read_t(itr, SceneCameraComp);
    const GapWindowComp*   win   = ecs_view_read_t(itr, GapWindowComp);
    SceneTransformComp*    trans = ecs_view_write_t(itr, SceneTransformComp);

    const GapVector winSize   = gap_window_param(win, GapParam_WindowSize);
    const GapVector cursorPos = gap_window_param(win, GapParam_CursorPos);
    const f32       aspect    = (f32)winSize.width / (f32)winSize.height;
    const GeoVector inputPos  = {cursorPos.x / (f32)winSize.x, cursorPos.y / (f32)winSize.y};

    if (trans && cam->flags & SceneCameraFlags_DebugGizmoTranslation) {
      const DebugGizmoId gizmoId = (DebugGizmoId)ecs_view_entity(itr);
      debug_gizmo_translation(gizmo, gizmoId, &trans->position, trans->rotation);
    }
    if (trans && cam->flags & SceneCameraFlags_DebugGizmoRotation) {
      const DebugGizmoId gizmoId = (DebugGizmoId)ecs_view_entity(itr);
      debug_gizmo_rotation(gizmo, gizmoId, trans->position, &trans->rotation);
    }
    if (cam->flags & SceneCameraFlags_DebugFrustum) {
      debug_camera_draw_frustum(shape, cam, trans, aspect);
    }
    if (cam->flags & SceneCameraFlags_DebugInputRay) {
      debug_camera_draw_input_ray(
          shape, text, collisionEnv, nameView, cam, trans, aspect, inputPos);
    }
  }
}

ecs_module_init(debug_camera_module) {
  ecs_register_comp(DebugCameraPanelComp);

  ecs_register_view(PanelUpdateView);
  ecs_register_view(WindowView);
  ecs_register_view(GlobalDrawView);
  ecs_register_view(DrawView);
  ecs_register_view(NameView);

  ecs_register_system(
      DebugCameraUpdatePanelSys, ecs_view_id(PanelUpdateView), ecs_view_id(WindowView));

  ecs_register_system(
      DebugCameraDrawSys,
      ecs_view_id(GlobalDrawView),
      ecs_view_id(DrawView),
      ecs_view_id(NameView));

  ecs_order(DebugCameraDrawSys, DebugOrder_CameraDebugDraw);
}

EcsEntityId debug_camera_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugCameraPanelComp,
      .panel  = ui_panel(.position = ui_vector(0.75f, 0.5f), .size = ui_vector(340, 340)),
      .window = window);
  return panelEntity;
}
