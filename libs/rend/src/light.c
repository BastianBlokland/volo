#include "asset_manager.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "ecs_world.h"
#include "geo_matrix.h"
#include "log_logger.h"
#include "rend_draw.h"
#include "rend_light.h"
#include "rend_register.h"
#include "rend_settings.h"

#include "light_internal.h"

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
  f32            attenuationLinear, attenuationQuadratic;
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

static f32 rend_light_point_radius(const RendLightPoint* point) {
  const GeoColor radiance   = rend_radiance_resolve(point->radiance);
  const f32      brightness = math_max(math_max(radiance.r, radiance.g), radiance.b);
  const f32      c          = 1.0f;                        // Constant term.
  const f32      l          = point->attenuationLinear;    // Linear term.
  const f32      q          = point->attenuationQuadratic; // Quadratic term.
  const f32      threshold  = 256.0f / 5.0f;
  return (-l + math_sqrt_f32(l * l - 4.0f * q * (c - threshold * brightness))) / (2.0f * q);
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
    const RendLightFlags flags = RendLightFlags_Shadow;
    rend_light_directional(light, settings->lightSunRotation, settings->lightSunRadiance, flags);
  }
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
        GeoVector direction; // x, y, z: direction, w: unused.
        GeoColor  radiance;  // r, g, b: radiance, a: unused.
      } LightDirData;
      ASSERT(sizeof(LightDirData) == 32, "Size needs to match the size defined in glsl");
      ASSERT(alignof(LightDirData) == 16, "Alignment needs to match the glsl alignment");

      typedef struct {
        ALIGNAS(16)
        GeoVector posScale;    // x, y, z: position, w: scale.
        GeoColor  radiance;    // r, g, b: radiance, a: unused.
        GeoVector attenuation; // x: constant term, y: linear term, z: quadratic term, w: unused.
      } LightPointData;
      ASSERT(sizeof(LightPointData) == 48, "Size needs to match the size defined in glsl");
      ASSERT(alignof(LightPointData) == 16, "Alignment needs to match the glsl alignment");

      switch (entry->type) {
      case RendLightType_Directional: {
        bool shadow = (entry->data_directional.flags & RendLightFlags_Shadow) != 0;
        if (shadow && renderer->hasShadow) {
          log_e("Only a single directional shadow is supported");
          shadow = false;
        }
        if (shadow) {
          renderer->hasShadow         = true;
          renderer->shadowTransMatrix = geo_matrix_from_quat(entry->data_directional.rotation);
          renderer->shadowProjMatrix  = geo_matrix_proj_ortho(200, 200, -100, 100);
        }
        const GeoVector direction = geo_quat_rotate(entry->data_directional.rotation, geo_forward);
        const GeoColor  radiance  = rend_radiance_resolve(entry->data_directional.radiance);
        const GeoBox    bounds    = geo_box_inverted3(); // Cannot be culled.
        if (radiance.r > f32_epsilon || radiance.g > f32_epsilon || radiance.b > f32_epsilon) {
          *rend_draw_add_instance_t(draw, LightDirData, tags, bounds) = (LightDirData){
              .direction = direction,
              .radiance  = radiance,
          };
        }
        break;
      }
      case RendLightType_Point: {
        if (entry->data_point.flags & RendLightFlags_Shadow) {
          log_e("Point-light shadows are unsupported");
        }
        const f32 radius = rend_light_point_radius(&entry->data_point);
        if (UNLIKELY(radius < f32_epsilon)) {
          continue;
        }
        const GeoBox bounds = geo_box_from_sphere(entry->data_point.pos, radius);
        *rend_draw_add_instance_t(draw, LightPointData, tags, bounds) = (LightPointData){
            .posScale.x    = entry->data_point.pos.x,
            .posScale.y    = entry->data_point.pos.y,
            .posScale.z    = entry->data_point.pos.z,
            .posScale.w    = radius,
            .radiance      = rend_radiance_resolve(entry->data_point.radiance),
            .attenuation.x = 1.0f, // Constant term.
            .attenuation.y = entry->data_point.attenuationLinear,
            .attenuation.z = entry->data_point.attenuationQuadratic,
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

  ecs_register_system(RendLightSunSys, ecs_view_id(GlobalView));

  ecs_register_system(
      RendLightRenderSys, ecs_view_id(GlobalView), ecs_view_id(LightView), ecs_view_id(DrawView));

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
    const f32            attenuationLinear,
    const f32            attenuationQuadratic,
    const RendLightFlags flags) {
  rend_light_add(
      comp,
      (RendLight){
          .type = RendLightType_Point,
          .data_point =
              {
                  .pos                  = pos,
                  .radiance             = radiance,
                  .attenuationLinear    = attenuationLinear,
                  .attenuationQuadratic = attenuationQuadratic,
                  .flags                = flags,
              },
      });
}

const GeoMatrix* rend_light_shadow_trans(const RendLightRendererComp* renderer) {
  return &renderer->shadowTransMatrix;
}

const GeoMatrix* rend_light_shadow_proj(const RendLightRendererComp* renderer) {
  return &renderer->shadowProjMatrix;
}
