#include "core_alloc.h"
#include "core_annotation.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "debug_gizmo.h"
#include "debug_grid.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "debug_stats.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "geo_query.h"
#include "input.h"
#include "scene_camera.h"
#include "scene_transform.h"

#define gizmo_ring_segments 32

static const f32 g_gizmoCollisionScale = 1.5f;
static const f32 g_gizmoSnapAngleDeg   = 45.0f;

static const struct {
  GeoVector normal;
  f32       length, radius;
  GeoColor  colorNormal, colorHovered;
} g_gizmoTranslationArrows[] = {
    {
        .normal       = {1, 0, 0},
        .length       = 0.75f,
        .radius       = 0.075f,
        .colorNormal  = {0.4f, 0, 0, 1},
        .colorHovered = {1, 0.05f, 0.05f, 1},
    },
    {
        .normal       = {0, 1, 0},
        .length       = 0.75f,
        .radius       = 0.075f,
        .colorNormal  = {0, 0.4f, 0, 1},
        .colorHovered = {0.05f, 1, 0.05f, 1},
    },
    {
        .normal       = {0, 0, 1},
        .length       = 0.75f,
        .radius       = 0.075f,
        .colorNormal  = {0, 0, 0.4f, 1},
        .colorHovered = {0.05f, 0.05f, 1, 1},
    },
};

static const struct {
  GeoVector normal, tangent;
  f32       radius, thickness;
  GeoColor  colorNormal, colorHovered;
} g_gizmoRotationRings[] = {
    {
        .normal       = {1, 0, 0},
        .tangent      = {0, 1, 0},
        .radius       = 0.5f,
        .thickness    = 0.02f,
        .colorNormal  = {0.4f, 0, 0, 1},
        .colorHovered = {1, 0.05f, 0.05f, 1},
    },
    {
        .normal       = {0, 1, 0},
        .tangent      = {0, 0, 1},
        .radius       = 0.5f,
        .thickness    = 0.02f,
        .colorNormal  = {0, 0.4f, 0, 1},
        .colorHovered = {0.05f, 1, 0.05f, 1},
    },
    {
        .normal       = {0, 0, 1},
        .tangent      = {1, 0, 0},
        .radius       = 0.5f,
        .thickness    = 0.02f,
        .colorNormal  = {0, 0, 0.4f, 1},
        .colorHovered = {0.05f, 0.05f, 1, 1},
    },
};

typedef enum {
  DebugGizmoType_Translation,
  DebugGizmoType_Rotation,

  DebugGizmoType_Count,
} DebugGizmoType;

typedef struct {
  DebugGizmoType type;
  DebugGizmoId   id;
  GeoVector      pos;
  GeoQuat        rot;
} DebugGizmoEntry;

typedef enum {
  DebugGizmoStatus_None,
  DebugGizmoStatus_Hovering,
  DebugGizmoStatus_Interacting,
} DebugGizmoStatus;

typedef enum {
  DebugGizmoSection_X,
  DebugGizmoSection_Y,
  DebugGizmoSection_Z,

  DebugGizmoSection_Count,
} DebugGizmoSection;

typedef struct {
  GeoVector basePos;
  GeoQuat   baseRot;
  GeoVector startPos; // Position where the interaction started.
  GeoVector result;
} DebugGizmoEditorTranslation;

typedef struct {
  GeoVector basePos;
  GeoQuat   baseRot;
  GeoVector startDelta; // From gizmo center to where the interaction started.
  GeoQuat   result;
} DebugGizmoEditorRotation;

ecs_comp_define(DebugGizmoComp) {
  DynArray     entries; // DebugGizmo[]
  GeoQueryEnv* queryEnv;

  DebugGizmoStatus  status;
  DebugGizmoType    activeType;
  DebugGizmoId      activeId;
  DebugGizmoSection activeSection;
  u32               interactingTicks;
  union {
    DebugGizmoEditorTranslation translation;
    DebugGizmoEditorRotation    rotation;
  } editor;
};

static void ecs_destruct_gizmo(void* data) {
  DebugGizmoComp* comp = data;
  dynarray_destroy(&comp->entries);
  geo_query_env_destroy(comp->queryEnv);
}

static const String g_gizmoSectionNames[] = {
    [DebugGizmoSection_X] = string_static("x"),
    [DebugGizmoSection_Y] = string_static("y"),
    [DebugGizmoSection_Z] = string_static("z"),
};
ASSERT(array_elems(g_gizmoSectionNames) == DebugGizmoSection_Count, "Missing section name");

static bool gizmo_is_hovered(const DebugGizmoComp* comp, const DebugGizmoId id) {
  return comp->status >= DebugGizmoStatus_Hovering && comp->activeId == id;
}

static bool gizmo_is_hovered_section(
    const DebugGizmoComp* comp, const DebugGizmoId id, const DebugGizmoSection section) {
  return gizmo_is_hovered(comp, id) && comp->activeSection == section;
}

static bool gizmo_is_interacting(const DebugGizmoComp* comp, const DebugGizmoId id) {
  return comp->status >= DebugGizmoStatus_Interacting && comp->activeId == id;
}

static bool gizmo_is_interacting_type(
    const DebugGizmoComp* comp, const DebugGizmoId id, const DebugGizmoType type) {
  return gizmo_is_interacting(comp, id) && comp->activeType == type;
}

ecs_view_define(GlobalUpdateView) {
  ecs_access_write(DebugStatsGlobalComp);
  ecs_access_write(DebugGizmoComp);
  ecs_access_write(InputManagerComp);
}

ecs_view_define(GlobalRenderView) {
  ecs_access_read(DebugGizmoComp);
  ecs_access_write(DebugShapeComp);
}

ecs_view_define(CameraView) {
  ecs_access_maybe_read(DebugGridComp);
  ecs_access_read(GapWindowComp);
  ecs_access_read(SceneCameraComp);
  ecs_access_read(SceneTransformComp);
}

/**
 * The shape-id encodes both the index of the gizmo as well as the section of the gizmo.
 * For example the x-arrow of a specific translation gizmo.
 */
static u64 gizmo_shape_id(const u32 i, const DebugGizmoSection s) { return i | ((u64)s << 32u); }
static u32 gizmo_shape_index(const u64 id) { return (u32)id; }
static DebugGizmoSection gizmo_shape_section(const u64 id) {
  return (DebugGizmoSection)(id >> 32u);
}

static const DebugGizmoEntry* gizmo_entry(const DebugGizmoComp* comp, const u32 index) {
  return dynarray_at_t(&comp->entries, index, DebugGizmoEntry);
}

static u32 gizmo_entry_index(const DebugGizmoComp* comp, const DebugGizmoEntry* entry) {
  return (u32)(entry - dynarray_begin_t(&comp->entries, DebugGizmoEntry));
}

static void gizmo_ring_points(
    const GeoVector center,
    const GeoQuat   rotation,
    const f32       radius,
    GeoVector       out[gizmo_ring_segments]) {
  const f32 segStep = math_pi_f32 * 2.0f / gizmo_ring_segments;
  for (u32 i = 0; i != gizmo_ring_segments; ++i) {
    const f32       angle = i * segStep;
    const GeoVector point = geo_vector(math_sin_f32(angle) * radius, math_cos_f32(angle) * radius);
    out[i]                = geo_vector_add(center, geo_quat_rotate(rotation, point));
  }
}

static void gizmo_ring_capsules(
    const GeoVector center,
    const GeoQuat   rotation,
    const f32       radius,
    const f32       thickness,
    GeoCapsule      out[gizmo_ring_segments]) {
  GeoVector points[gizmo_ring_segments];
  gizmo_ring_points(center, rotation, radius, points);
  for (u32 i = 0; i != gizmo_ring_segments; ++i) {
    const GeoVector pointA = points[i];
    const GeoVector pointB = points[(i + 1) % gizmo_ring_segments];
    out[i]                 = (GeoCapsule){.line = {pointA, pointB}, .radius = thickness};
  }
}

static void gizmo_register_translation(DebugGizmoComp* comp, const DebugGizmoEntry* entry) {
  diag_assert(entry->type == DebugGizmoType_Translation);

  /**
   * Register collision shapes for the translation arrows.
   */
  for (u32 i = 0; i != array_elems(g_gizmoTranslationArrows); ++i) {
    const GeoVector dir       = geo_quat_rotate(entry->rot, g_gizmoTranslationArrows[i].normal);
    const f32       length    = g_gizmoTranslationArrows[i].length;
    const GeoVector lineStart = entry->pos;
    const GeoVector lineEnd   = geo_vector_add(lineStart, geo_vector_mul(dir, length));

    const u64 shapeId = gizmo_shape_id(gizmo_entry_index(comp, entry), (DebugGizmoSection)i);
    geo_query_insert_capsule(
        comp->queryEnv,
        (GeoCapsule){
            .line   = {.a = lineStart, .b = lineEnd},
            .radius = g_gizmoTranslationArrows[i].radius * g_gizmoCollisionScale,
        },
        shapeId);
  }
}

static void gizmo_register_rotation(DebugGizmoComp* comp, const DebugGizmoEntry* entry) {
  diag_assert(entry->type == DebugGizmoType_Rotation);

  /**
   * Register collision shapes for the rotation rings.
   */
  GeoCapsule capsules[gizmo_ring_segments];
  for (u32 i = 0; i != array_elems(g_gizmoRotationRings); ++i) {
    const GeoVector normal    = g_gizmoRotationRings[i].normal;
    const GeoVector tangent   = g_gizmoRotationRings[i].tangent;
    const GeoQuat   ringRot   = geo_quat_mul(entry->rot, geo_quat_look(normal, tangent));
    const f32       radius    = g_gizmoRotationRings[i].radius;
    const f32       thickness = g_gizmoRotationRings[i].thickness * g_gizmoCollisionScale;
    const u64       shapeId = gizmo_shape_id(gizmo_entry_index(comp, entry), (DebugGizmoSection)i);

    gizmo_ring_capsules(entry->pos, ringRot, radius, thickness, capsules);
    for (u32 segment = 0; segment != gizmo_ring_segments; ++segment) {
      geo_query_insert_capsule(comp->queryEnv, capsules[segment], shapeId);
    }
  }
}

static void gizmo_register(DebugGizmoComp* comp, const DebugGizmoEntry* entry) {
  switch (entry->type) {
  case DebugGizmoType_Translation:
    gizmo_register_translation(comp, entry);
    break;
  case DebugGizmoType_Rotation:
    gizmo_register_rotation(comp, entry);
    break;
  case DebugGizmoType_Count:
    UNREACHABLE
  }
}

static void gizmo_interaction_hover(
    DebugGizmoComp* comp, const DebugGizmoEntry* entry, const DebugGizmoSection section) {
  comp->status        = DebugGizmoStatus_Hovering;
  comp->activeType    = entry->type;
  comp->activeId      = entry->id;
  comp->activeSection = section;
}

static void gizmo_interaction_start(
    DebugGizmoComp* comp, const DebugGizmoEntry* entry, const DebugGizmoSection section) {
  comp->status           = DebugGizmoStatus_Interacting;
  comp->activeType       = entry->type;
  comp->activeId         = entry->id;
  comp->activeSection    = section;
  comp->interactingTicks = 0;

  switch (entry->type) {
  case DebugGizmoType_Translation:
    comp->editor.translation = (DebugGizmoEditorTranslation){
        .basePos = entry->pos,
        .baseRot = entry->rot,
        .result  = entry->pos,
    };
    break;
  case DebugGizmoType_Rotation:
    comp->editor.rotation = (DebugGizmoEditorRotation){
        .basePos = entry->pos,
        .baseRot = entry->rot,
        .result  = entry->rot,
    };
    break;
  case DebugGizmoType_Count:
    UNREACHABLE;
  }
}

static void gizmo_interaction_cancel(DebugGizmoComp* comp) { comp->status = DebugGizmoStatus_None; }

static bool gizmo_interaction_is_blocked(const InputManagerComp* input) {
  /**
   * Disallow gizmo interation while Ui is being hovered.
   */
  return (input_blockers(input) & InputBlocker_HoveringUi) != 0;
}

/**
 * Pick an interaction plane based on the desired editing section (axis) and input ray.
 */
static GeoPlane gizmo_translation_plane(
    const GeoVector         basePos,
    const GeoQuat           baseRot,
    const DebugGizmoSection section,
    const GeoRay*           ray) {
  diag_assert(section >= DebugGizmoSection_X && section <= DebugGizmoSection_Z);

  // Pick the best normal based on the camera direction.
  static const GeoVector g_normals[][2] = {
      [DebugGizmoSection_X] = {{0, 1, 0}, {0, 0, 1}},
      [DebugGizmoSection_Y] = {{0, 0, 1}, {1, 0, 0}},
      [DebugGizmoSection_Z] = {{0, 1, 0}, {1, 0, 0}},
  };
  const GeoVector nrmA = geo_quat_rotate(baseRot, g_normals[section][0]);
  const GeoVector nrmB = geo_quat_rotate(baseRot, g_normals[section][1]);
  const f32       dotA = geo_vector_dot(ray->dir, nrmA);
  GeoVector       nrm  = math_abs(dotA) > 0.5f ? nrmA : nrmB;
  if (geo_vector_dot(ray->dir, nrm) > 0) {
    nrm = geo_vector_mul(nrm, -1.0f); // Axis is pointing await from the camera; flip.
  }
  return geo_plane_at(nrm, basePos);
}

static void gizmo_update_interaction_translation(
    DebugGizmoComp*       comp,
    DebugStatsGlobalComp* stats,
    const GapWindowComp*  window,
    const DebugGridComp*  grid,
    const GeoRay*         ray) {
  DebugGizmoEditorTranslation* data    = &comp->editor.translation;
  const DebugGizmoSection      section = comp->activeSection;

  diag_assert(comp->activeType == DebugGizmoType_Translation);
  diag_assert(section >= DebugGizmoSection_X && section <= DebugGizmoSection_Z);

  const GeoPlane plane   = gizmo_translation_plane(data->basePos, data->baseRot, section, ray);
  const f32      hitDist = geo_plane_intersect_ray(&plane, ray);
  if (hitDist < 0 || hitDist > 1e3f) {
    return; // No intersection with the interaction plane.
  }
  const GeoVector inputPos = geo_ray_position(ray, hitDist);
  if (!comp->interactingTicks) {
    data->startPos = inputPos;
  }
  const GeoVector axis  = geo_quat_rotate(data->baseRot, g_gizmoTranslationArrows[section].normal);
  const GeoVector delta = geo_vector_project(geo_vector_sub(inputPos, data->startPos), axis);
  data->result          = geo_vector_add(data->basePos, delta);

  if (grid && gap_window_key_down(window, GapKey_Shift)) {
    debug_grid_snap_axis(grid, &data->result, (u8)section);
  }

  const f32 statDeltaMag = geo_vector_mag(geo_vector_sub(data->result, data->basePos));
  debug_stats_notify(
      stats,
      string_lit("Gizmo delta"),
      fmt_write_scratch("{}", fmt_float(statDeltaMag, .minDecDigits = 4, .maxDecDigits = 4)));
}

static f32 gizmo_vector_angle(const GeoVector from, const GeoVector to, const GeoVector axis) {
  const GeoVector fromNorm   = geo_vector_norm(from);
  const GeoVector toNorm     = geo_vector_norm(to);
  const GeoVector tangent    = geo_vector_cross3(axis, fromNorm);
  const f32       dotTo      = geo_vector_dot(fromNorm, toNorm);
  const f32       dotTangent = geo_vector_dot(tangent, toNorm);
  return math_acos_f32(math_clamp_f32(dotTo, -1.0f, 1.0f)) * math_sign(dotTangent);
}

static void gizmo_update_interaction_rotation(
    DebugGizmoComp*       comp,
    DebugStatsGlobalComp* stats,
    const GapWindowComp*  window,
    const GeoRay*         ray) {
  DebugGizmoEditorRotation* data    = &comp->editor.rotation;
  const DebugGizmoSection   section = comp->activeSection;

  diag_assert(comp->activeType == DebugGizmoType_Rotation);
  diag_assert(section >= DebugGizmoSection_X && section <= DebugGizmoSection_Z);

  GeoVector axis = geo_quat_rotate(data->baseRot, g_gizmoRotationRings[section].normal);
  if (geo_vector_dot(ray->dir, axis) > 0) {
    axis = geo_vector_mul(axis, -1.0f); // Axis is pointing await from the camera; flip.
  }
  const GeoPlane plane   = geo_plane_at(axis, data->basePos);
  const f32      hitDist = geo_plane_intersect_ray(&plane, ray);
  if (hitDist < 0 || hitDist > 1e3f) {
    return; // No intersection with the interaction plane.
  }
  const GeoVector delta = geo_vector_sub(geo_ray_position(ray, hitDist), data->basePos);
  if (!comp->interactingTicks) {
    data->startDelta = delta;
  }
  f32 angle = gizmo_vector_angle(data->startDelta, delta, axis);
  if (gap_window_key_down(window, GapKey_Shift)) {
    const f32 snapAngleRad = g_gizmoSnapAngleDeg * math_deg_to_rad;
    angle                  = math_round_f32(angle / snapAngleRad) * snapAngleRad;
  }
  data->result = geo_quat_mul(geo_quat_angle_axis(axis, angle), data->baseRot);

  debug_stats_notify(
      stats,
      string_lit("Gizmo delta"),
      fmt_write_scratch(
          "{} degrees", fmt_float(angle * math_rad_to_deg, .minDecDigits = 1, .maxDecDigits = 1)));
}

static void gizmo_update_interaction(
    DebugGizmoComp*           comp,
    DebugStatsGlobalComp*     stats,
    const InputManagerComp*   input,
    const GapWindowComp*      window,
    const DebugGridComp*      grid,
    const SceneCameraComp*    camera,
    const SceneTransformComp* cameraTrans) {

  const bool      inputDown    = gap_window_key_down(window, GapKey_MouseLeft);
  const GeoVector inputNormPos = geo_vector(input_cursor_x(input), input_cursor_y(input));
  const f32       inputAspect  = input_cursor_aspect(input);
  const GeoRay    inputRay     = scene_camera_ray(camera, cameraTrans, inputAspect, inputNormPos);
  const bool      isBlocked    = gizmo_interaction_is_blocked(input);

  const DebugGizmoEntry* hoverEntry   = null;
  DebugGizmoSection      hoverSection = 0;
  GeoQueryRayHit         hit;
  if (!isBlocked && geo_query_ray(comp->queryEnv, &inputRay, &hit) && hit.time < 1e3f) {
    hoverEntry   = gizmo_entry(comp, gizmo_shape_index(hit.shapeId));
    hoverSection = gizmo_shape_section(hit.shapeId);
  }

  if (comp->status == DebugGizmoStatus_None && hoverEntry) {
    gizmo_interaction_hover(comp, hoverEntry, hoverSection);
    return;
  }

  const bool isHovering    = comp->status == DebugGizmoStatus_Hovering;
  const bool isInteracting = comp->status == DebugGizmoStatus_Interacting;

  if ((isHovering && !hoverEntry) || (isInteracting && !inputDown)) {
    gizmo_interaction_cancel(comp);
    return;
  }

  if (isHovering && (comp->activeId != hoverEntry->id || comp->activeSection != hoverSection)) {
    gizmo_interaction_hover(comp, hoverEntry, hoverSection);
    return;
  }

  if (isHovering && inputDown) {
    gizmo_interaction_start(comp, hoverEntry, hoverSection);
    return;
  }

  if (isInteracting) {
    debug_stats_notify(
        stats, string_lit("Gizmo section"), g_gizmoSectionNames[comp->activeSection]);

    switch (comp->activeType) {
    case DebugGizmoType_Translation:
      gizmo_update_interaction_translation(comp, stats, window, grid, &inputRay);
      break;
    case DebugGizmoType_Rotation:
      gizmo_update_interaction_rotation(comp, stats, window, &inputRay);
      break;
    case DebugGizmoType_Count:
      UNREACHABLE
    }
    ++comp->interactingTicks;
  }
}

static void debug_gizmo_create(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_add_t(
      world,
      entity,
      DebugGizmoComp,
      .entries  = dynarray_create_t(g_alloc_heap, DebugGizmoEntry, 16),
      .queryEnv = geo_query_env_create(g_alloc_heap));
}

ecs_system_define(DebugGizmoUpdateSys) {
  // Initialize the global gizmo component.
  const EcsEntityId globalEntity = ecs_world_global(world);
  if (!ecs_world_has_t(world, globalEntity, DebugGizmoComp)) {
    debug_gizmo_create(world, globalEntity);
    return;
  }

  EcsView*     globalView = ecs_world_view_t(world, GlobalUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, globalEntity);
  if (!globalItr) {
    return;
  }
  DebugStatsGlobalComp* stats = ecs_view_write_t(globalItr, DebugStatsGlobalComp);
  DebugGizmoComp*       gizmo = ecs_view_write_t(globalItr, DebugGizmoComp);
  InputManagerComp*     input = ecs_view_write_t(globalItr, InputManagerComp);

  // Register all gizmos that where active in the last frame.
  geo_query_env_clear(gizmo->queryEnv);
  for (u32 i = 0; i != gizmo->entries.size; ++i) {
    gizmo_register(gizmo, gizmo_entry(gizmo, i));
  }

  // Update the editor.
  EcsView* cameraView = ecs_world_view_t(world, CameraView);
  if (ecs_view_contains(cameraView, input_active_window(input))) {
    EcsIterator*              camItr      = ecs_view_at(cameraView, input_active_window(input));
    const GapWindowComp*      window      = ecs_view_read_t(camItr, GapWindowComp);
    const DebugGridComp*      grid        = ecs_view_read_t(camItr, DebugGridComp);
    const SceneCameraComp*    camera      = ecs_view_read_t(camItr, SceneCameraComp);
    const SceneTransformComp* cameraTrans = ecs_view_read_t(camItr, SceneTransformComp);

    gizmo_update_interaction(gizmo, stats, input, window, grid, camera, cameraTrans);
  }

  // Update input blockers.
  input_blocker_update(input, InputBlocker_HoveringGizmo, gizmo->status > DebugGizmoStatus_None);

  // Clear last frame's entries.
  dynarray_clear(&gizmo->entries);
}

static GeoColor
gizmo_translation_arrow_color(const DebugGizmoComp* comp, const DebugGizmoId id, const u32 index) {
  diag_assert(index < 3);

  if (gizmo_is_hovered_section(comp, id, (DebugGizmoSection)index)) {
    return g_gizmoTranslationArrows[index].colorHovered;
  }
  if (comp->status >= DebugGizmoStatus_Interacting) {
    return geo_color_gray; // Another gizmo (or section) is being interacted with.
  }
  return g_gizmoTranslationArrows[index].colorNormal;
}

static f32
gizmo_translation_arrow_radius(const DebugGizmoComp* comp, const DebugGizmoId id, const u32 index) {
  diag_assert(index < 3);

  const f32 base = g_gizmoTranslationArrows[index].radius;
  if (gizmo_is_hovered_section(comp, id, (DebugGizmoSection)index)) {
    return base * 1.1f;
  }
  if (comp->status >= DebugGizmoStatus_Interacting) {
    return base * 0.75f; // Another gizmo (or section) is being interacted with.
  }
  return base;
}

static void gizmo_draw_translation(
    const DebugGizmoComp* comp, DebugShapeComp* shape, const DebugGizmoEntry* entry) {
  diag_assert(entry->type == DebugGizmoType_Translation);

  const bool      isInteracting = gizmo_is_interacting_type(comp, entry->id, entry->type);
  const GeoVector pos           = isInteracting ? comp->editor.translation.result : entry->pos;

  // Draw center point.
  debug_sphere(shape, pos, 0.025f, geo_color_white, DebugShape_Overlay);

  // Draw arrows.
  for (u32 i = 0; i != array_elems(g_gizmoTranslationArrows); ++i) {
    const GeoVector dir     = geo_quat_rotate(entry->rot, g_gizmoTranslationArrows[i].normal);
    const f32       length  = g_gizmoTranslationArrows[i].length;
    const f32       radius  = gizmo_translation_arrow_radius(comp, entry->id, i);
    const GeoVector lineEnd = geo_vector_add(pos, geo_vector_mul(dir, length));
    const GeoColor  color   = gizmo_translation_arrow_color(comp, entry->id, i);

    debug_arrow(shape, pos, lineEnd, radius, color);
  }
}

static GeoColor
gizmo_rotation_ring_color(const DebugGizmoComp* comp, const DebugGizmoId id, const u32 index) {
  diag_assert(index < 3);

  if (gizmo_is_hovered_section(comp, id, (DebugGizmoSection)index)) {
    return g_gizmoRotationRings[index].colorHovered;
  }
  if (comp->status >= DebugGizmoStatus_Interacting) {
    return geo_color_gray; // Another gizmo (or section) is being interacted with.
  }
  return g_gizmoRotationRings[index].colorNormal;
}

static f32
gizmo_rotation_ring_thickness(const DebugGizmoComp* comp, const DebugGizmoId id, const u32 index) {
  diag_assert(index < 3);

  const f32 base = g_gizmoRotationRings[index].thickness;
  if (gizmo_is_hovered_section(comp, id, (DebugGizmoSection)index)) {
    return base * 1.1f;
  }
  if (comp->status >= DebugGizmoStatus_Interacting) {
    return base * 0.5f; // Another gizmo (or section) is being interacted with.
  }
  return base;
}

static void gizmo_draw_rotation(
    const DebugGizmoComp* comp, DebugShapeComp* shape, const DebugGizmoEntry* entry) {
  diag_assert(entry->type == DebugGizmoType_Rotation);

  const bool    isInteracting = gizmo_is_interacting_type(comp, entry->id, entry->type);
  const GeoQuat rot           = isInteracting ? comp->editor.rotation.result : entry->rot;

  // Draw center point.
  debug_sphere(shape, entry->pos, 0.025f, geo_color_white, DebugShape_Overlay);

  // Draw rings.
  GeoCapsule capsules[gizmo_ring_segments];
  for (u32 i = 0; i != array_elems(g_gizmoRotationRings); ++i) {
    const GeoVector normal    = g_gizmoRotationRings[i].normal;
    const GeoVector tangent   = g_gizmoRotationRings[i].tangent;
    const GeoQuat   ringRot   = geo_quat_mul(rot, geo_quat_look(normal, tangent));
    const f32       radius    = g_gizmoRotationRings[i].radius;
    const f32       thickness = gizmo_rotation_ring_thickness(comp, entry->id, i);

    gizmo_ring_capsules(entry->pos, ringRot, radius, thickness, capsules);
    for (u32 segment = 0; segment != gizmo_ring_segments; ++segment) {
      const GeoCapsule*    capsule = &capsules[segment];
      const GeoColor       color   = gizmo_rotation_ring_color(comp, entry->id, i);
      const DebugShapeMode mode    = DebugShape_Overlay;
      debug_cylinder(shape, capsule->line.a, capsule->line.b, capsule->radius, color, mode);
    }
  }
}

ecs_system_define(DebugGizmoRenderSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalRenderView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const DebugGizmoComp* gizmo = ecs_view_read_t(globalItr, DebugGizmoComp);
  DebugShapeComp*       shape = ecs_view_write_t(globalItr, DebugShapeComp);

  dynarray_for_t(&gizmo->entries, DebugGizmoEntry, entry) {
    switch (entry->type) {
    case DebugGizmoType_Translation:
      gizmo_draw_translation(gizmo, shape, entry);
      break;
    case DebugGizmoType_Rotation:
      gizmo_draw_rotation(gizmo, shape, entry);
      break;
    case DebugGizmoType_Count:
      UNREACHABLE
    }
  }
}

ecs_module_init(debug_gizmo_module) {
  ecs_register_comp(DebugGizmoComp, .destructor = ecs_destruct_gizmo);

  ecs_register_view(GlobalUpdateView);
  ecs_register_view(GlobalRenderView);
  ecs_register_view(CameraView);

  ecs_register_system(DebugGizmoUpdateSys, ecs_view_id(GlobalUpdateView), ecs_view_id(CameraView));
  ecs_order(DebugGizmoUpdateSys, DebugOrder_GizmoUpdate);

  ecs_register_system(DebugGizmoRenderSys, ecs_view_id(GlobalRenderView));
  ecs_order(DebugGizmoRenderSys, DebugOrder_GizmoRender);
}

bool debug_gizmo_translation(
    DebugGizmoComp* comp, const DebugGizmoId id, GeoVector* translation, const GeoQuat rotation) {

  *dynarray_push_t(&comp->entries, DebugGizmoEntry) = (DebugGizmoEntry){
      .type = DebugGizmoType_Translation,
      .id   = id,
      .pos  = *translation,
      .rot  = rotation,
  };

  const bool isInteracting = gizmo_is_interacting_type(comp, id, DebugGizmoType_Translation);
  if (isInteracting) {
    *translation = comp->editor.translation.result;
  }
  return isInteracting;
}

bool debug_gizmo_rotation(
    DebugGizmoComp* comp, const DebugGizmoId id, const GeoVector translation, GeoQuat* rotation) {

  *dynarray_push_t(&comp->entries, DebugGizmoEntry) = (DebugGizmoEntry){
      .type = DebugGizmoType_Rotation,
      .id   = id,
      .pos  = translation,
      .rot  = *rotation,
  };

  const bool isInteracting = gizmo_is_interacting_type(comp, id, DebugGizmoType_Rotation);
  if (isInteracting) {
    *rotation = comp->editor.rotation.result;
  }
  return isInteracting;
}

bool debug_gizmo_scale_uniform(
    DebugGizmoComp* comp, const DebugGizmoId id, const GeoVector translation, f32* scale) {
  (void)comp;
  (void)id;
  (void)translation;
  (void)scale;
  return false;
}
