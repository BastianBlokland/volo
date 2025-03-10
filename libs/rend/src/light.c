#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_math.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "geo_box_rotated.h"
#include "geo_matrix.h"
#include "log_logger.h"
#include "rend_light.h"
#include "rend_object.h"
#include "rend_register.h"
#include "rend_settings.h"
#include "scene_camera.h"
#include "scene_light.h"
#include "scene_terrain.h"
#include "scene_transform.h"

#include "light_internal.h"

static const f32 g_lightMinAmbient        = 0.01f; // NOTE: Total black looks pretty bad.
static const f32 g_lightDirMaxShadowDist  = 250.0f;
static const f32 g_lightDirShadowStepSize = 10.0f;
static const f32 g_worldHeight            = 10.0f;

typedef enum {
  RendLightType_Directional,
  RendLightType_Point,
  RendLightType_Ambient,

  RendLightType_Count,
} RendLightType;

typedef enum {
  RendLightVariation_Normal,
  RendLightVariation_Debug,

  RendLightVariation_Count,
} RendLightVariation;

enum { RendLightObj_Count = RendLightType_Count * RendLightVariation_Count };

typedef struct {
  GeoQuat        rotation;
  GeoColor       radiance;
  RendLightFlags flags;
} RendLightDirectional;

typedef struct {
  GeoVector      pos;
  GeoColor       radiance;
  f32            radius;
  RendLightFlags flags;
} RendLightPoint;

typedef struct {
  f32 intensity;
} RendLightAmbient;

typedef struct {
  RendLightType type;
  union {
    RendLightDirectional data_directional;
    RendLightPoint       data_point;
    RendLightAmbient     data_ambient;
  };
} RendLight;

// clang-format off
static const String g_lightGraphics[RendLightObj_Count] = {
    [RendLightType_Directional + RendLightVariation_Normal] = string_static("graphics/light/light_directional.graphic"),
    [RendLightType_Point       + RendLightVariation_Normal] = string_static("graphics/light/light_point.graphic"),
    [RendLightType_Point       + RendLightVariation_Debug]  = string_static("graphics/light/light_point_debug.graphic"),
};
// clang-format on

ecs_comp_define(RendLightRendererComp) {
  EcsEntityId objEntities[RendLightObj_Count];
  f32         ambientIntensity;
  bool        hasShadow;
  GeoMatrix   shadowTransMatrix, shadowProjMatrix;
};

typedef struct {
  DynArray entries; // RendLightDebug[]
} RendLightDebugStorage;

ecs_comp_define(RendLightComp) {
  DynArray              entries; // RendLight[]
  RendLightDebugStorage debug;
};

static void ecs_destruct_light(void* data) {
  RendLightComp* comp = data;
  dynarray_destroy(&comp->entries);
  dynarray_destroy(&comp->debug.entries);
}

ecs_view_define(GlobalInitView) {
  ecs_access_without(RendLightRendererComp);
  ecs_access_write(AssetManagerComp);
}

ecs_view_define(GlobalView) {
  ecs_access_read(RendSettingsGlobalComp);
  ecs_access_read(SceneTerrainComp);
  ecs_access_write(RendLightComp);
  ecs_access_write(RendLightRendererComp);
}

ecs_view_define(LightView) { ecs_access_write(RendLightComp); }

ecs_view_define(ObjView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // Only access the render objects we create.
  ecs_access_write(RendObjectComp);
}

ecs_view_define(CameraView) {
  ecs_access_read(GapWindowAspectComp);
  ecs_access_read(SceneCameraComp);
  ecs_access_maybe_read(SceneTransformComp);
}

ecs_view_define(LightPointInstView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_read(SceneLightPointComp);
  ecs_access_maybe_read(SceneScaleComp);
}

ecs_view_define(LightDirInstView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_read(SceneLightDirComp);
  ecs_access_maybe_read(SceneScaleComp);
}

ecs_view_define(LightAmbientInstView) {
  ecs_access_read(SceneLightAmbientComp);
  ecs_access_maybe_read(SceneScaleComp);
}

static u32 rend_obj_index(const RendLightType type, const RendLightVariation variation) {
  return (u32)type + (u32)variation;
}

static EcsEntityId rend_light_obj_create(
    EcsWorld*                world,
    AssetManagerComp*        assets,
    const RendLightType      type,
    const RendLightVariation var) {
  const u32 objIndex = rend_obj_index(type, var);
  if (string_is_empty(g_lightGraphics[objIndex])) {
    return 0;
  }

  const EcsEntityId entity        = ecs_world_entity_create(world);
  RendObjectComp*   obj           = rend_object_create(world, entity, RendObjectFlags_None);
  const EcsEntityId graphicEntity = asset_lookup(world, assets, g_lightGraphics[objIndex]);
  rend_object_set_resource(obj, RendObjectRes_Graphic, graphicEntity);
  return entity;
}

static void rend_light_renderer_create(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId      global   = ecs_world_global(world);
  RendLightRendererComp* renderer = ecs_world_add_t(world, global, RendLightRendererComp);

  for (RendLightType type = 0; type != RendLightType_Count; ++type) {
    for (RendLightVariation var = 0; var != RendLightVariation_Count; ++var) {
      const u32 objIndex              = rend_obj_index(type, var);
      renderer->objEntities[objIndex] = rend_light_obj_create(world, assets, type, var);
    }
  }
}

ecs_system_define(RendLightInitSys) {
  EcsView*     globalInitView = ecs_world_view_t(world, GlobalInitView);
  EcsIterator* globalInitItr  = ecs_view_maybe_at(globalInitView, ecs_world_global(world));
  if (globalInitItr) {
    AssetManagerComp* assets = ecs_view_write_t(globalInitItr, AssetManagerComp);

    rend_light_renderer_create(world, assets);
    rend_light_create(world, ecs_world_global(world)); // Global light component for convenience.
  }
}

static void rend_light_debug_clear(RendLightDebugStorage* debug) {
  dynarray_clear(&debug->entries);
}

static void rend_light_debug_push(
    RendLightDebugStorage* debug, const RendLightDebugType type, const GeoVector frustum[8]) {
  RendLightDebug* entry = dynarray_push_t(&debug->entries, RendLightDebug);
  entry->type           = type;
  mem_cpy(mem_var(entry->frustum), mem_create(frustum, sizeof(GeoVector) * 8));
}

INLINE_HINT static void rend_light_add(RendLightComp* comp, const RendLight light) {
  *((RendLight*)dynarray_push(&comp->entries, 1).ptr) = light;
}

static GeoColor rend_radiance_resolve(const GeoColor radiance) {
  return (GeoColor){
      .r = radiance.r * radiance.a,
      .g = radiance.g * radiance.a,
      .b = radiance.b * radiance.a,
      .a = 1.0f,
  };
}

static f32 rend_light_brightness(const GeoColor radiance) {
  return math_max(math_max(radiance.r, radiance.g), radiance.b);
}

ecs_system_define(RendLightPushSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not yet available.
  }
  RendLightComp* light = ecs_view_write_t(globalItr, RendLightComp);

  // Push all point-lights.
  EcsView* pointLights = ecs_world_view_t(world, LightPointInstView);
  for (EcsIterator* itr = ecs_view_itr(pointLights); ecs_view_walk(itr);) {
    const SceneTransformComp*  transformComp = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*      scaleComp     = ecs_view_read_t(itr, SceneScaleComp);
    const SceneLightPointComp* pointComp     = ecs_view_read_t(itr, SceneLightPointComp);

    GeoColor radiance = pointComp->radiance;
    f32      radius   = pointComp->radius;
    if (scaleComp) {
      radiance.a *= scaleComp->scale;
      radius *= scaleComp->scale;
    }
    const RendLightFlags flags = RendLightFlags_None;
    rend_light_point(light, transformComp->position, radiance, radius, flags);
  }

  // Push all directional lights.
  EcsView* dirLights = ecs_world_view_t(world, LightDirInstView);
  for (EcsIterator* itr = ecs_view_itr(dirLights); ecs_view_walk(itr);) {
    const SceneTransformComp* transformComp = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scaleComp     = ecs_view_read_t(itr, SceneScaleComp);
    const SceneLightDirComp*  dirComp       = ecs_view_read_t(itr, SceneLightDirComp);

    GeoColor radiance = dirComp->radiance;
    if (scaleComp) {
      radiance.a *= scaleComp->scale;
    }
    RendLightFlags flags = RendLightFlags_None;
    if (dirComp->shadows) {
      flags |= RendLightFlags_Shadow;
    }
    if (dirComp->coverage) {
      flags |= RendLightFlags_CoverageMask;
    }
    rend_light_directional(light, transformComp->rotation, radiance, flags);
  }

  // Push all ambient lights.
  EcsView* ambientLights = ecs_world_view_t(world, LightAmbientInstView);
  for (EcsIterator* itr = ecs_view_itr(ambientLights); ecs_view_walk(itr);) {
    const SceneScaleComp*        scaleComp   = ecs_view_read_t(itr, SceneScaleComp);
    const SceneLightAmbientComp* ambientComp = ecs_view_read_t(itr, SceneLightAmbientComp);

    f32 intensity = ambientComp->intensity;
    if (scaleComp) {
      intensity *= scaleComp->scale;
    }
    rend_light_ambient(light, intensity);
  }
}

static void rend_clip_frustum_far_dist(GeoVector frustum[PARAM_ARRAY_SIZE(8)], const f32 maxDist) {
  for (u32 i = 0; i != 4; ++i) {
    const u32       idxNear = i;
    const u32       idxFar  = 4 + i;
    const GeoVector toBack  = geo_vector_sub(frustum[idxFar], frustum[idxNear]);
    const f32       sqrDist = geo_vector_mag_sqr(toBack);
    if (sqrDist > (maxDist * maxDist)) {
      const GeoVector toBackDir = geo_vector_div(toBack, math_sqrt_f32(sqrDist));
      frustum[idxFar] = geo_vector_add(frustum[idxNear], geo_vector_mul(toBackDir, maxDist));
    }
  }
}

static void
rend_clip_frustum_far_to_plane(GeoVector frustum[PARAM_ARRAY_SIZE(8)], const GeoPlane* clipPlane) {
  for (u32 i = 0; i != 4; ++i) {
    const u32       idxNear    = i;
    const u32       idxFar     = 4 + i;
    const GeoVector dirToFront = geo_vector_norm(geo_vector_sub(frustum[idxNear], frustum[idxFar]));
    const GeoRay    rayToFront = {.dir = dirToFront, .point = frustum[idxFar]};
    const f32       farClipDist = geo_plane_intersect_ray(clipPlane, &rayToFront);
    if (farClipDist > 0) {
      frustum[idxFar] = geo_ray_position(&rayToFront, farClipDist);
    }
  }
}

static void
rend_clip_frustum_far_to_bounds(GeoVector frustum[PARAM_ARRAY_SIZE(8)], const GeoBox* clipBounds) {
  rend_clip_frustum_far_to_plane(frustum, &(GeoPlane){geo_up, clipBounds->max.y});
  rend_clip_frustum_far_to_plane(frustum, &(GeoPlane){geo_down, -clipBounds->min.y});
  rend_clip_frustum_far_to_plane(frustum, &(GeoPlane){geo_right, clipBounds->max.x});
  rend_clip_frustum_far_to_plane(frustum, &(GeoPlane){geo_left, -clipBounds->min.x});
  rend_clip_frustum_far_to_plane(frustum, &(GeoPlane){geo_forward, clipBounds->max.z});
  rend_clip_frustum_far_to_plane(frustum, &(GeoPlane){geo_backward, -clipBounds->min.z});
}

static GeoBox rend_light_shadow_discretize(GeoBox box, const f32 step) {
  box.min = geo_vector_mul(geo_vector_round_nearest(geo_vector_div(box.min, step)), step);
  box.max = geo_vector_mul(geo_vector_round_nearest(geo_vector_div(box.max, step)), step);
  return geo_box_dilate(&box, geo_vector(step * 0.5f, step * 0.5f, step * 0.5f));
}

static GeoMatrix rend_light_compute_dir_shadow_proj(
    const SceneTerrainComp*    terrain,
    const GapWindowAspectComp* winAspect,
    const SceneCameraComp*     cam,
    const SceneTransformComp*  camTrans,
    const GeoQuat              lightRot,
    RendLightDebugStorage*     debug) {
  // Compute the world-space camera frustum corners.
  GeoVector       frustum[8];
  const GeoVector winCamMin = geo_vector(0, 0), winCamMax = geo_vector(1, 1);
  scene_camera_frustum_corners(cam, camTrans, winAspect->ratio, winCamMin, winCamMax, frustum);

  // Clip the camera frustum to the region that actually contains content.
  rend_clip_frustum_far_dist(frustum, g_lightDirMaxShadowDist);
  if (scene_terrain_loaded(terrain)) {
    const GeoBox terrainBounds = scene_terrain_bounds(terrain);
    const GeoBox worldBounds   = geo_box_dilate(&terrainBounds, geo_vector(0, g_worldHeight, 0));
    rend_clip_frustum_far_to_bounds(frustum, &worldBounds);
  }

  if (debug) {
    rend_light_debug_push(debug, RendLightDebug_ShadowFrustumTarget, frustum);
  }

  // Compute the bounding box in light-space.
  const GeoQuat lightRotInv = geo_quat_inverse(lightRot);
  GeoBox        bounds      = geo_box_inverted3();
  for (u32 i = 0; i != array_elems(frustum); ++i) {
    const GeoVector localCorner = geo_quat_rotate(lightRotInv, frustum[i]);
    bounds                      = geo_box_encapsulate(&bounds, localCorner);
  }

  if (debug) {
    const GeoBoxRotated local = {.box = bounds, .rotation = geo_quat_ident};
    const GeoBoxRotated world = geo_box_rotated_transform3(&local, geo_vector(0), lightRot, 1.0f);
    GeoVector           shadowCorners[8];
    geo_box_rotated_corners3(&world, shadowCorners);
    rend_light_debug_push(debug, RendLightDebug_ShadowFrustumRaw, shadowCorners);
  }

  /**
   * Discretize the bounds so the shadow projection stays the same for small movements, this reduces
   * the visible shadow 'shimmering'.
   */
  bounds = rend_light_shadow_discretize(bounds, g_lightDirShadowStepSize);

  if (debug) {
    const GeoBoxRotated local = {.box = bounds, .rotation = geo_quat_ident};
    const GeoBoxRotated world = geo_box_rotated_transform3(&local, geo_vector(0), lightRot, 1.0f);
    GeoVector           shadowCorners[8];
    geo_box_rotated_corners3(&world, shadowCorners);
    rend_light_debug_push(debug, RendLightDebug_ShadowFrustumDiscrete, shadowCorners);
  }

  return geo_matrix_proj_ortho_box(
      bounds.min.x, bounds.max.x, bounds.min.y, bounds.max.y, bounds.min.z, bounds.max.z);
}

ecs_system_define(RendLightRenderSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not yet available.
  }

  RendLightRendererComp*        renderer = ecs_view_write_t(globalItr, RendLightRendererComp);
  const RendSettingsGlobalComp* settings = ecs_view_read_t(globalItr, RendSettingsGlobalComp);
  const SceneTerrainComp*       terrain  = ecs_view_read_t(globalItr, SceneTerrainComp);

  const bool debugLight         = (settings->flags & RendGlobalFlags_DebugLight) != 0;
  const bool debugLightFreeze   = (settings->flags & RendGlobalFlags_DebugLightFreeze) != 0;
  const RendLightVariation var  = debugLight ? RendLightVariation_Debug : RendLightVariation_Normal;
  const SceneTags          tags = SceneTags_Light;

  renderer->hasShadow        = false;
  renderer->ambientIntensity = 0.0f;

  // Clear debug output from the previous frame.
  if (debugLight && !debugLightFreeze) {
    for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, LightView)); ecs_view_walk(itr);) {
      rend_light_debug_clear(&ecs_view_write_t(itr, RendLightComp)->debug);
    }
  }

  EcsIterator* camItr = ecs_view_first(ecs_world_view_t(world, CameraView));
  if (!camItr) {
    return; // No Camera found.
  }
  /**
   * TODO: Support multiple camera's (requires multiple objs for directional lights with shadows).
   */
  const GapWindowAspectComp* winAspect = ecs_view_read_t(camItr, GapWindowAspectComp);
  const SceneCameraComp*     cam       = ecs_view_read_t(camItr, SceneCameraComp);
  const SceneTransformComp*  camTrans  = ecs_view_read_t(camItr, SceneTransformComp);

  EcsView*     objView = ecs_world_view_t(world, ObjView);
  EcsIterator* objItr  = ecs_view_itr(objView);

  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, LightView)); ecs_view_walk(itr);) {
    RendLightComp*         light        = ecs_view_write_t(itr, RendLightComp);
    RendLightDebugStorage* debugStorage = (debugLight && !debugLightFreeze) ? &light->debug : null;

    dynarray_for_t(&light->entries, RendLight, entry) {
      if (entry->type == RendLightType_Ambient) {
        renderer->ambientIntensity += entry->data_ambient.intensity;
        continue;
      }
      const u32 objIndex = rend_obj_index(entry->type, var);
      if (!renderer->objEntities[objIndex]) {
        continue;
      }
      ecs_view_jump(objItr, renderer->objEntities[objIndex]);
      RendObjectComp* obj = ecs_view_write_t(objItr, RendObjectComp);

      typedef struct {
        ALIGNAS(16)
        GeoVector direction;     // x, y, z: direction, w: unused.
        GeoVector radianceFlags; // x, y, z: radiance, a: flags.
        GeoVector shadowParams;  // x: filterSize, y, z, w: unused.
        GeoMatrix shadowViewProj;
      } LightDirData;
      ASSERT(sizeof(LightDirData) == 112, "Size needs to match the size defined in glsl");

      typedef struct {
        ALIGNAS(16)
        GeoVector posScale;             // x, y, z: position, w: scale.
        GeoColor  radianceAndRadiusInv; // r, g, b: radiance, a: inverse radius (1.0 / radius).
      } LightPointData;
      ASSERT(sizeof(LightPointData) == 32, "Size needs to match the size defined in glsl");

      switch (entry->type) {
      case RendLightType_Directional: {
        const GeoColor radiance = rend_radiance_resolve(entry->data_directional.radiance);
        if (rend_light_brightness(radiance) < 0.01f) {
          continue;
        }
        bool shadow = (entry->data_directional.flags & RendLightFlags_Shadow) != 0;
        if (shadow && renderer->hasShadow) {
          log_e("Only a single directional shadow is supported");
          shadow = false;
        }
        GeoMatrix shadowViewProj;
        if (shadow) {
          const GeoQuat   transRot = entry->data_directional.rotation;
          const GeoMatrix transMat = geo_matrix_from_quat(transRot);
          const GeoMatrix viewMat  = geo_matrix_inverse(&transMat);

          renderer->hasShadow         = true;
          renderer->shadowTransMatrix = transMat;
          renderer->shadowProjMatrix  = rend_light_compute_dir_shadow_proj(
              terrain, winAspect, cam, camTrans, transRot, debugStorage);

          shadowViewProj = geo_matrix_mul(&renderer->shadowProjMatrix, &viewMat);
        } else {
          shadowViewProj = (GeoMatrix){0};
        }
        const GeoVector direction = geo_quat_rotate(entry->data_directional.rotation, geo_forward);
        const GeoBox    bounds    = geo_box_inverted3(); // Cannot be culled.
        *rend_object_add_instance_t(obj, LightDirData, tags, bounds) = (LightDirData){
            .direction       = direction,
            .radianceFlags.x = radiance.r,
            .radianceFlags.y = radiance.g,
            .radianceFlags.z = radiance.b,
            .radianceFlags.w = bits_u32_as_f32(entry->data_directional.flags),
            .shadowParams.x  = settings->shadowFilterSize,
            .shadowViewProj  = shadowViewProj,
        };
        break;
      }
      case RendLightType_Point: {
        if (entry->data_point.flags & RendLightFlags_Shadow) {
          log_e("Point-light shadows are unsupported");
        }
        const GeoVector pos      = entry->data_point.pos;
        const GeoColor  radiance = rend_radiance_resolve(entry->data_directional.radiance);
        const f32       radius   = entry->data_point.radius;
        if (UNLIKELY(rend_light_brightness(radiance) < 0.01f || radius < f32_epsilon)) {
          continue;
        }
        const GeoBox bounds = geo_box_from_sphere(pos, radius);
        *rend_object_add_instance_t(obj, LightPointData, tags, bounds) = (LightPointData){
            .posScale.x             = pos.x,
            .posScale.y             = pos.y,
            .posScale.z             = pos.z,
            .posScale.w             = radius,
            .radianceAndRadiusInv.r = radiance.r,
            .radianceAndRadiusInv.g = radiance.g,
            .radianceAndRadiusInv.b = radiance.b,
            .radianceAndRadiusInv.a = 1.0f / radius,
        };
        break;
      }
      default:
        diag_crash();
      }
    }
    dynarray_clear(&light->entries);
  }
}

ecs_module_init(rend_light_module) {
  ecs_register_comp(RendLightRendererComp);
  ecs_register_comp(RendLightComp, .destructor = ecs_destruct_light);

  ecs_register_view(GlobalView);
  ecs_register_view(GlobalInitView);
  ecs_register_view(LightView);
  ecs_register_view(ObjView);
  ecs_register_view(CameraView);
  ecs_register_view(LightPointInstView);
  ecs_register_view(LightDirInstView);
  ecs_register_view(LightAmbientInstView);

  ecs_register_system(RendLightInitSys, ecs_view_id(GlobalInitView));

  ecs_register_system(
      RendLightPushSys,
      ecs_view_id(GlobalView),
      ecs_view_id(LightPointInstView),
      ecs_view_id(LightDirInstView),
      ecs_view_id(LightAmbientInstView));

  ecs_register_system(
      RendLightRenderSys,
      ecs_view_id(GlobalView),
      ecs_view_id(LightView),
      ecs_view_id(ObjView),
      ecs_view_id(CameraView));

  // NOTE: +1 is added to allow the vfx system (which also adds lights) to run in parallel with
  // instance object update without the created lights rendering a frame too late.
  ecs_order(RendLightRenderSys, RendOrder_ObjectUpdate + 1);
}

RendLightComp* rend_light_create(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(
      world,
      entity,
      RendLightComp,
      .entries = dynarray_create_t(g_allocHeap, RendLight, 4),
      .debug   = dynarray_create_t(g_allocHeap, RendLightDebug, 0));
}

usize rend_light_debug_count(const RendLightComp* light) { return light->debug.entries.size; }

const RendLightDebug* rend_light_debug_data(const RendLightComp* light) {
  return dynarray_begin_t(&light->debug.entries, RendLightDebug);
}

void rend_light_directional(
    RendLightComp*       comp,
    const GeoQuat        rotation,
    const GeoColor       radiance,
    const RendLightFlags flags) {
  rend_light_add(
      comp,
      (RendLight){
          .type = RendLightType_Directional,
          .data_directional =
              {
                  .rotation = rotation,
                  .radiance = radiance,
                  .flags    = flags,
              },
      });
}

void rend_light_point(
    RendLightComp*       comp,
    const GeoVector      pos,
    const GeoColor       radiance,
    const f32            radius,
    const RendLightFlags flags) {
  rend_light_add(
      comp,
      (RendLight){
          .type = RendLightType_Point,
          .data_point =
              {
                  .pos      = pos,
                  .radiance = radiance,
                  .radius   = radius,
                  .flags    = flags,
              },
      });
}

void rend_light_ambient(RendLightComp* comp, const f32 intensity) {
  rend_light_add(
      comp,
      (RendLight){
          .type         = RendLightType_Ambient,
          .data_ambient = {.intensity = intensity},
      });
}

f32 rend_light_ambient_intensity(const RendLightRendererComp* renderer) {
  return math_max(renderer->ambientIntensity, g_lightMinAmbient);
}

bool rend_light_has_shadow(const RendLightRendererComp* renderer) { return renderer->hasShadow; }

const GeoMatrix* rend_light_shadow_trans(const RendLightRendererComp* renderer) {
  return &renderer->shadowTransMatrix;
}

const GeoMatrix* rend_light_shadow_proj(const RendLightRendererComp* renderer) {
  return &renderer->shadowProjMatrix;
}
