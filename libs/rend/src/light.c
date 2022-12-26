#include "asset_manager.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "ecs_world.h"
#include "rend_draw.h"
#include "rend_light.h"
#include "rend_register.h"

typedef enum {
  RendLightType_Point,

  RendLightType_Count,
} RendLightType;

typedef struct {
  GeoVector pos;
  GeoColor  radiance;
  f32       attenuationLinear, attenuationQuadratic;
} RendLightPoint;

typedef struct {
  RendLightType type;
  union {
    RendLightPoint data_point;
  };
} RendLight;

static const String g_lightGraphics[RendLightType_Count] = {
    [RendLightType_Point] = string_static("graphics/light/light_point.gra"),
};

ecs_comp_define_public(RendLightSettingsComp);

ecs_comp_define(RendLightRendererComp) { EcsEntityId drawEntities[RendLightType_Count]; };

ecs_comp_define(RendLightComp) {
  DynArray entries; // RendLight[]
};

static void ecs_destruct_light(void* data) {
  RendLightComp* comp = data;
  dynarray_destroy(&comp->entries);
}

ecs_view_define(AssetManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LightRendererView) { ecs_access_write(RendLightRendererComp); }
ecs_view_define(LightView) { ecs_access_write(RendLightComp); }
ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }

static AssetManagerComp* rend_asset_manager(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, AssetManagerView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, AssetManagerComp) : null;
}

static RendLightRendererComp* rend_light_renderer(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, LightRendererView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, RendLightRendererComp) : null;
}

static EcsEntityId
rend_light_draw_create(EcsWorld* world, AssetManagerComp* assets, const RendLightType type) {
  diag_assert(!string_is_empty(g_lightGraphics[type]));

  const EcsEntityId entity        = ecs_world_entity_create(world);
  RendDrawComp*     draw          = rend_draw_create(world, entity, RendDrawFlags_None);
  const EcsEntityId graphicEntity = asset_lookup(world, assets, g_lightGraphics[type]);
  rend_draw_set_graphic(draw, graphicEntity);
  return entity;
}

static void rend_light_renderer_create(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId      global   = ecs_world_global(world);
  RendLightRendererComp* renderer = ecs_world_add_t(world, global, RendLightRendererComp);

  for (RendLightType type = 0; type != RendLightType_Count; ++type) {
    renderer->drawEntities[type] = rend_light_draw_create(world, assets, type);
  }
}

static RendLightSettingsComp* rend_light_settings_create(EcsWorld* world) {
  const EcsEntityId      global        = ecs_world_global(world);
  RendLightSettingsComp* lightSettings = ecs_world_add_t(world, global, RendLightSettingsComp);
  rend_light_settings_to_default(lightSettings);
  return lightSettings;
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
  const f32      threshold  = 256.0f / 20.0f;
  return (-l + math_sqrt_f32(l * l - 4.0f * q * (c - threshold * brightness))) / (2.0f * q);
}

ecs_system_define(RendLightRenderSys) {
  AssetManagerComp* assets = rend_asset_manager(world);
  if (!assets) {
    return; // Asset manager hasn't been initialized yet.
  }

  RendLightRendererComp* renderer = rend_light_renderer(world);
  if (!renderer) {
    rend_light_renderer_create(world, assets);
    rend_light_settings_create(world);
    rend_light_create(world, ecs_world_global(world)); // Global light component for convenience.
    return;
  }

  EcsView*     drawView = ecs_world_view_t(world, DrawView);
  EcsIterator* drawItr  = ecs_view_itr(drawView);

  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, LightView)); ecs_view_walk(itr);) {
    RendLightComp* light = ecs_view_write_t(itr, RendLightComp);
    dynarray_for_t(&light->entries, RendLight, entry) {
      ecs_view_jump(drawItr, renderer->drawEntities[entry->type]);
      RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);

      typedef struct {
        ALIGNAS(16)
        GeoVector posScale;    // x, y, z: position, w: scale.
        GeoColor  radiance;    // r, g, b: radiance, a: unused.
        GeoVector attenuation; // x: constant term, y: linear term, z: quadratic term, w: unused.
      } LightPointData;
      ASSERT(sizeof(LightPointData) == 48, "Size needs to match the size defined in glsl");
      ASSERT(alignof(LightPointData) == 16, "Alignment needs to match the glsl alignment");

      switch (entry->type) {
      case RendLightType_Point: {
        const f32 radius = rend_light_point_radius(&entry->data_point);
        if (UNLIKELY(radius < f32_epsilon)) {
          continue;
        }
        const GeoBox bounds = geo_box_from_sphere(entry->data_point.pos, radius);
        *rend_draw_add_instance_t(draw, LightPointData, SceneTags_None, bounds) = (LightPointData){
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
  ecs_register_comp(RendLightSettingsComp);
  ecs_register_comp(RendLightRendererComp);
  ecs_register_comp(RendLightComp, .destructor = ecs_destruct_light);

  ecs_register_view(AssetManagerView);
  ecs_register_view(LightRendererView);
  ecs_register_view(LightView);
  ecs_register_view(DrawView);

  ecs_register_system(
      RendLightRenderSys,
      ecs_view_id(AssetManagerView),
      ecs_view_id(LightRendererView),
      ecs_view_id(LightView),
      ecs_view_id(DrawView));

  ecs_order(RendLightRenderSys, RendOrder_DrawCollect);
}

RendLightComp* rend_light_create(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(
      world, entity, RendLightComp, .entries = dynarray_create_t(g_alloc_heap, RendLight, 4));
}

void rend_light_point(
    RendLightComp*  comp,
    const GeoVector pos,
    const GeoColor  radiance,
    const f32       attenuationLinear,
    const f32       attenuationQuadratic) {

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
              },
      });
}

void rend_light_settings_to_default(RendLightSettingsComp* s) {
  s->sunRadiance = geo_color(1.0f, 0.9f, 0.8f, 3.0f);
  s->sunRotation = geo_quat_from_euler(geo_vector_mul(geo_vector(50, 15, 0), math_deg_to_rad));
  s->ambient     = 0.1f;
}
