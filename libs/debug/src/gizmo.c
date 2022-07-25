#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "debug_gizmo.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "geo_query.h"
#include "input.h"
#include "scene_camera.h"
#include "scene_transform.h"

static const struct {
  GeoVector dir;
  f32       length, radius;
  GeoColor  colorNormal, colorHovered;
} g_gizmoTranslationArrows[] = {
    {
        .dir          = {1, 0, 0},
        .length       = 0.75f,
        .radius       = 0.075f,
        .colorNormal  = {0.4f, 0, 0, 0.75f},
        .colorHovered = {1, 0.05f, 0.05f, 1},
    },
    {
        .dir          = {0, 1, 0},
        .length       = 0.75f,
        .radius       = 0.075f,
        .colorNormal  = {0, 0.4f, 0, 0.75f},
        .colorHovered = {0.05f, 1, 0.05f, 1},
    },
    {
        .dir          = {0, 0, 1},
        .length       = 0.75f,
        .radius       = 0.075f,
        .colorNormal  = {0, 0, 0.4f, 0.75f},
        .colorHovered = {0.05f, 0.05f, 1, 1},
    },
};

typedef enum {
  DebugGizmoType_Translation,

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
  u32       interactingTicks;
  GeoVector basePos;
  GeoQuat   baseRot;
  GeoVector startPos; // Position where the interaction started.
  GeoVector result;
} DebugGizmoEditorTranslation;

ecs_comp_define(DebugGizmoComp) {
  DynArray     entries; // DebugGizmo[]
  GeoQueryEnv* queryEnv;

  DebugGizmoStatus  status;
  DebugGizmoType    activeType;
  DebugGizmoId      activeId;
  DebugGizmoSection activeSection;
  union {
    DebugGizmoEditorTranslation translation;
  } editor;
};

static void ecs_destruct_gizmo(void* data) {
  DebugGizmoComp* comp = data;
  dynarray_destroy(&comp->entries);
  geo_query_env_destroy(comp->queryEnv);
}

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
  ecs_access_write(DebugGizmoComp);
  ecs_access_write(InputManagerComp);
}

ecs_view_define(GlobalRenderView) {
  ecs_access_read(DebugGizmoComp);
  ecs_access_write(DebugShapeComp);
}

ecs_view_define(CameraView) {
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

static void gizmo_register_translation(DebugGizmoComp* comp, const DebugGizmoEntry* entry) {
  diag_assert(entry->type == DebugGizmoType_Translation);

  /**
   * Register collision shapes for the translation arrows.
   */
  for (u32 i = 0; i != array_elems(g_gizmoTranslationArrows); ++i) {
    const GeoVector dir       = geo_quat_rotate(entry->rot, g_gizmoTranslationArrows[i].dir);
    const f32       length    = g_gizmoTranslationArrows[i].length;
    const GeoVector lineStart = entry->pos;
    const GeoVector lineEnd   = geo_vector_add(lineStart, geo_vector_mul(dir, length));

    const u64 shapeId = gizmo_shape_id(gizmo_entry_index(comp, entry), (DebugGizmoSection)i);
    geo_query_insert_capsule(
        comp->queryEnv,
        (GeoCapsule){
            .line   = {.a = lineStart, .b = lineEnd},
            .radius = g_gizmoTranslationArrows[i].radius,
        },
        shapeId);
  }
}

static void gizmo_register(DebugGizmoComp* comp, const DebugGizmoEntry* entry) {
  switch (entry->type) {
  case DebugGizmoType_Translation:
    return gizmo_register_translation(comp, entry);
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
  comp->status        = DebugGizmoStatus_Interacting;
  comp->activeType    = entry->type;
  comp->activeId      = entry->id;
  comp->activeSection = section;

  switch (entry->type) {
  case DebugGizmoType_Translation:
    comp->editor.translation = (DebugGizmoEditorTranslation){
        .basePos = entry->pos,
        .baseRot = entry->rot,
        .result  = entry->pos,
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

  // Flip the axis if its pointing away from the camera.
  if (geo_vector_dot(ray->dir, nrm) > 0) {
    nrm = geo_vector_mul(nrm, -1.0f);
  }
  return geo_plane_at(nrm, basePos);
}

static void gizmo_update_interaction_translation(DebugGizmoComp* comp, const GeoRay* ray) {
  DebugGizmoEditorTranslation* data    = &comp->editor.translation;
  const DebugGizmoSection      section = comp->activeSection;

  diag_assert(comp->activeType == DebugGizmoType_Translation);
  diag_assert(section >= DebugGizmoSection_X && section <= DebugGizmoSection_Z);

  const GeoPlane plane   = gizmo_translation_plane(data->basePos, data->baseRot, section, ray);
  const f32      hitDist = geo_plane_intersect_ray(&plane, ray);
  if (hitDist < 0) {
    return; // No intersection with the interaction plane.
  }
  const GeoVector inputPos = geo_ray_position(ray, hitDist);
  if (!data->interactingTicks++) {
    data->startPos = inputPos;
  }
  const GeoVector axis  = geo_quat_rotate(data->baseRot, g_gizmoTranslationArrows[section].dir);
  const GeoVector delta = geo_vector_sub(inputPos, data->startPos);
  data->result          = geo_vector_add(data->basePos, geo_vector_project(delta, axis));
}

static void gizmo_update_interaction(
    DebugGizmoComp*           comp,
    const InputManagerComp*   input,
    const GapWindowComp*      window,
    const SceneCameraComp*    camera,
    const SceneTransformComp* cameraTrans) {

  const bool      inputDown    = gap_window_key_down(window, GapKey_MouseLeft);
  const GeoVector inputNormPos = geo_vector(input_cursor_x(input), input_cursor_y(input));
  const f32       inputAspect  = input_cursor_aspect(input);
  const GeoRay    inputRay     = scene_camera_ray(camera, cameraTrans, inputAspect, inputNormPos);

  const DebugGizmoEntry* hoverEntry   = null;
  DebugGizmoSection      hoverSection = 0;
  GeoQueryRayHit         hit;
  if (!gizmo_interaction_is_blocked(input) && geo_query_ray(comp->queryEnv, &inputRay, &hit)) {
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
    switch (comp->activeType) {
    case DebugGizmoType_Translation:
      gizmo_update_interaction_translation(comp, &inputRay);
      break;
    case DebugGizmoType_Count:
      UNREACHABLE
    }
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
  DebugGizmoComp*   gizmo = ecs_view_write_t(globalItr, DebugGizmoComp);
  InputManagerComp* input = ecs_view_write_t(globalItr, InputManagerComp);

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
    const SceneCameraComp*    camera      = ecs_view_read_t(camItr, SceneCameraComp);
    const SceneTransformComp* cameraTrans = ecs_view_read_t(camItr, SceneTransformComp);

    gizmo_update_interaction(gizmo, input, window, camera, cameraTrans);
  }

  // Update input blockers.
  input_blocker_update(input, InputBlocker_HoveringGizmo, gizmo->status > DebugGizmoStatus_None);

  // Clear last frame's entries.
  dynarray_clear(&gizmo->entries);
}

static GeoColor gizmo_translation_arrow_color(
    const DebugGizmoComp* comp, const DebugGizmoId id, const u32 arrowIndex) {
  diag_assert(arrowIndex < 3);

  if (gizmo_is_hovered_section(comp, id, (DebugGizmoSection)arrowIndex)) {
    return g_gizmoTranslationArrows[arrowIndex].colorHovered;
  }
  if (comp->status >= DebugGizmoStatus_Interacting) {
    return geo_color(1, 1, 1, 0.25f); // Another gizmo (or section) is being interacted with.
  }
  return g_gizmoTranslationArrows[arrowIndex].colorNormal;
}

static void gizmo_draw_translation(
    const DebugGizmoComp* comp, DebugShapeComp* shape, const DebugGizmoEntry* entry) {
  diag_assert(entry->type == DebugGizmoType_Translation);

  const bool      isInteracting = gizmo_is_interacting_type(comp, entry->id, entry->type);
  const GeoVector pos           = isInteracting ? comp->editor.translation.result : entry->pos;

  // Draw all translation arrows.
  for (u32 i = 0; i != array_elems(g_gizmoTranslationArrows); ++i) {
    const GeoVector dir       = geo_quat_rotate(entry->rot, g_gizmoTranslationArrows[i].dir);
    const f32       length    = g_gizmoTranslationArrows[i].length;
    const f32       radius    = g_gizmoTranslationArrows[i].radius;
    const GeoVector lineStart = geo_vector_add(pos, geo_vector_mul(dir, 0.02f));
    const GeoVector lineEnd   = geo_vector_add(pos, geo_vector_mul(dir, length));
    const GeoColor  color     = gizmo_translation_arrow_color(comp, entry->id, i);

    debug_arrow(shape, lineStart, lineEnd, radius, color);
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
