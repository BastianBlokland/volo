#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_math.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "geo_matrix.h"
#include "log_logger.h"
#include "rend_draw.h"
#include "rend_light.h"
#include "rend_register.h"
#include "rend_settings.h"
#include "scene_camera.h"

#include "light_internal.h"

static const f32 g_lightDirMaxShadowDist  = 250.0f;
static const f32 g_lightDirShadowStepSize = 15.0f;

// TODO: Dynamically compute the world bounds based on content.
static const GeoBox g_lightWorldBounds = {
    .min = {.x = -250.0f, .y = 0.0f, .z = -250.0f},
    .max = {.x = 250.0f, .y = 25.0f, .z = 250.0f},
};

typedef enum {
  RendLightType_Directional,
  RendLightType_Point,

  RendLightType_Count,
} RendLightType;

typedef enum {
  RendLightVariation_Normal,
  RendLightVariation_Debug,

  RendLightVariation_Count,
} RendLightVariation;

enum { RendLightDraw_Count = RendLightType_Count * RendLightVariation_Count };

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
  RendLightType type;
  union {
    RendLightDirectional data_directional;
    RendLightPoint       data_point;
  };
} RendLight;

// clang-format off
static const String g_lightGraphics[RendLightDraw_Count] = {
    [RendLightType_Directional + RendLightVariation_Normal] = string_static("graphics/light/light_directional.gra"),
    [RendLightType_Point       + RendLightVariation_Normal] = string_static("graphics/light/light_point.gra"),
    [RendLightType_Point       + RendLightVariation_Debug]  = string_static("graphics/light/light_point_debug.gra"),
};
// clang-format on

ecs_comp_define(RendLightRendererComp) {
  EcsEntityId drawEntities[RendLightDraw_Count];
  bool        hasShadow;
  GeoMatrix   shadowTransMatrix, shadowProjMatrix;
};

ecs_comp_define(RendLightComp) {
  DynArray entries; // RendLight[]
};

static void ecs_destruct_light(void* data) {
  RendLightComp* comp = data;
  dynarray_destroy(&comp->entries);
}

ecs_view_define(GlobalView) {
  ecs_access_maybe_write(RendLightComp);
  ecs_access_maybe_write(RendLightRendererComp);
  ecs_access_read(RendSettingsGlobalComp);
  ecs_access_write(AssetManagerComp);
}
ecs_view_define(LightView) { ecs_access_write(RendLightComp); }
ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }

ecs_view_define(CameraView) {
  ecs_access_read(GapWindowComp);
  ecs_access_read(SceneCameraComp);
  ecs_access_maybe_read(SceneTransformComp);
}

static u32 rend_draw_index(const RendLightType type, const RendLightVariation variation) {
  return (u32)type + (u32)variation;
}

static EcsEntityId rend_light_draw_create(
    EcsWorld*                world,
    AssetManagerComp*        assets,
    const RendLightType      type,
    const RendLightVariation var) {
  const u32 drawIndex = rend_draw_index(type, var);
  if (string_is_empty(g_lightGraphics[drawIndex])) {
    return 0;
  }

  const EcsEntityId entity        = ecs_world_entity_create(world);
  RendDrawComp*     draw          = rend_draw_create(world, entity, RendDrawFlags_Light);
  const EcsEntityId graphicEntity = asset_lookup(world, assets, g_lightGraphics[drawIndex]);
  rend_draw_set_graphic(draw, graphicEntity);
  return entity;
}

static void rend_light_renderer_create(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId      global   = ecs_world_global(world);
  RendLightRendererComp* renderer = ecs_world_add_t(world, global, RendLightRendererComp);

  for (RendLightType type = 0; type != RendLightType_Count; ++type) {
    for (RendLightVariation var = 0; var != RendLightVariation_Count; ++var) {
      const u32 drawIndex               = rend_draw_index(type, var);
      renderer->drawEntities[drawIndex] = rend_light_draw_create(world, assets, type, var);
    }
  }
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

ecs_system_define(RendLightSunSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not yet available.
  }
  const RendSettingsGlobalComp* settings = ecs_view_read_t(globalItr, RendSettingsGlobalComp);
  RendLightComp*                light    = ecs_view_write_t(globalItr, RendLightComp);
  if (light) {
    RendLightFlags flags = RendLightFlags_None;
    if (settings->flags & RendGlobalFlags_SunShadows) {
      flags |= RendLightFlags_Shadow;
    }
    if (settings->flags & RendGlobalFlags_SunCoverage) {
      flags |= RendLightFlags_CoverageMask;
    }
    rend_light_directional(light, settings->lightSunRotation, settings->lightSunRadiance, flags);
  }
}

static void rend_clip_frustum_far_dist(GeoVector frustum[8], const f32 maxDist) {
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

static void rend_clip_frustum_far_to_plane(GeoVector frustum[8], const GeoPlane* clipPlane) {
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

static void rend_clip_frustum_far_to_bounds(GeoVector frustum[8], const GeoBox* clipBounds) {
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

static f32 rend_win_aspect(const GapWindowComp* win) {
  const GapVector winSize = gap_window_param(win, GapParam_WindowSize);
  if (!winSize.width || !winSize.height) {
    return 1.0f;
  }
  return (f32)winSize.width / (f32)winSize.height;
}

static GeoMatrix rend_light_dir_shadow_proj(
    const GapWindowComp*      win,
    const SceneCameraComp*    cam,
    const SceneTransformComp* camTrans,
    const GeoMatrix*          lightViewMatrix) {
  // Compute the world-space camera frustum corners.
  GeoVector       frustum[8];
  f32             winAspect = rend_win_aspect(win);
  const GeoVector winCamMin = geo_vector(0, 0), winCamMax = geo_vector(1, 1);
  scene_camera_frustum_corners(cam, camTrans, winAspect, winCamMin, winCamMax, frustum);

  // Clip the camera frustum to the region that actually contains content.
  rend_clip_frustum_far_dist(frustum, g_lightDirMaxShadowDist);
  rend_clip_frustum_far_to_bounds(frustum, &g_lightWorldBounds);

  // Compute the bounding box in light-space.
  GeoBox bounds = geo_box_inverted3();
  for (u32 i = 0; i != array_elems(frustum); ++i) {
    const GeoVector localCorner = geo_matrix_transform3_point(lightViewMatrix, frustum[i]);
    bounds                      = geo_box_encapsulate(&bounds, localCorner);
  }

  /**
   * Discretize the bounds so the shadow projection stays the same for small movements, this reduces
   * the visible shadow 'shimmering'.
   */
  bounds = rend_light_shadow_discretize(bounds, g_lightDirShadowStepSize);

  return geo_matrix_proj_ortho_box(
      bounds.min.x, bounds.max.x, bounds.min.y, bounds.max.y, bounds.min.z, bounds.max.z);
}

ecs_system_define(RendLightRenderSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not yet available.
  }

  AssetManagerComp*             assets   = ecs_view_write_t(globalItr, AssetManagerComp);
  RendLightRendererComp*        renderer = ecs_view_write_t(globalItr, RendLightRendererComp);
  const RendSettingsGlobalComp* settings = ecs_view_read_t(globalItr, RendSettingsGlobalComp);
  if (!renderer) {
    rend_light_renderer_create(world, assets);
    rend_light_create(world, ecs_world_global(world)); // Global light component for convenience.
    return;
  }

  const bool               debugLight = (settings->flags & RendGlobalFlags_DebugLight) != 0;
  const RendLightVariation var  = debugLight ? RendLightVariation_Debug : RendLightVariation_Normal;
  const SceneTags          tags = SceneTags_Light;

  renderer->hasShadow = false;

  EcsIterator* camItr = ecs_view_first(ecs_world_view_t(world, CameraView));
  if (!camItr) {
    return; // No Camera found.
  }
  /**
   * TODO: Support multiple camera's (requires multiple draws for directional lights with shadows).
   */
  const GapWindowComp*      win      = ecs_view_read_t(camItr, GapWindowComp);
  const SceneCameraComp*    cam      = ecs_view_read_t(camItr, SceneCameraComp);
  const SceneTransformComp* camTrans = ecs_view_read_t(camItr, SceneTransformComp);

  EcsView*     drawView = ecs_world_view_t(world, DrawView);
  EcsIterator* drawItr  = ecs_view_itr(drawView);

  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, LightView)); ecs_view_walk(itr);) {
    RendLightComp* light = ecs_view_write_t(itr, RendLightComp);
    dynarray_for_t(&light->entries, RendLight, entry) {
      const u32 drawIndex = rend_draw_index(entry->type, var);
      if (!renderer->drawEntities[drawIndex]) {
        continue;
      }
      ecs_view_jump(drawItr, renderer->drawEntities[drawIndex]);
      RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);

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
          const GeoMatrix transMat = geo_matrix_from_quat(entry->data_directional.rotation);
          const GeoMatrix viewMat  = geo_matrix_inverse(&transMat);

          renderer->hasShadow         = true;
          renderer->shadowTransMatrix = transMat;
          renderer->shadowProjMatrix  = rend_light_dir_shadow_proj(win, cam, camTrans, &viewMat);

          shadowViewProj = geo_matrix_mul(&renderer->shadowProjMatrix, &viewMat);
        } else {
          shadowViewProj = (GeoMatrix){0};
        }
        const GeoVector direction = geo_quat_rotate(entry->data_directional.rotation, geo_forward);
        const GeoBox    bounds    = geo_box_inverted3(); // Cannot be culled.
        *rend_draw_add_instance_t(draw, LightDirData, tags, bounds) = (LightDirData){
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
        *rend_draw_add_instance_t(draw, LightPointData, tags, bounds) = (LightPointData){
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
  ecs_register_view(LightView);
  ecs_register_view(DrawView);
  ecs_register_view(CameraView);

  ecs_register_system(RendLightSunSys, ecs_view_id(GlobalView));

  ecs_register_system(
      RendLightRenderSys,
      ecs_view_id(GlobalView),
      ecs_view_id(LightView),
      ecs_view_id(DrawView),
      ecs_view_id(CameraView));

  ecs_order(RendLightRenderSys, RendOrder_DrawCollect);
}

RendLightComp* rend_light_create(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(
      world, entity, RendLightComp, .entries = dynarray_create_t(g_alloc_heap, RendLight, 4));
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

bool rend_light_has_shadow(const RendLightRendererComp* renderer) { return renderer->hasShadow; }

const GeoMatrix* rend_light_shadow_trans(const RendLightRendererComp* renderer) {
  return &renderer->shadowTransMatrix;
}

const GeoMatrix* rend_light_shadow_proj(const RendLightRendererComp* renderer) {
  return &renderer->shadowProjMatrix;
}
