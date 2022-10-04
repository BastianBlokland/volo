#include "asset_atlas.h"
#include "asset_manager.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_draw.h"
#include "scene_tag.h"

#include "particle_internal.h"

static const String g_vfxParticleGraphic = string_static("graphics/vfx/particle.gra");
static const String g_vfxParticleAtlas   = string_static("textures/vfx/particle.atl");

typedef struct {
  ALIGNAS(16)
  f32 atlasEntriesPerDim;
  f32 invAtlasEntriesPerDim;
} VfxParticleMetaData;

ASSERT(sizeof(VfxParticleMetaData) == 16, "Size needs to match the size defined in glsl");

typedef struct {
  ALIGNAS(16)
  GeoVector data1;    // xyz: position, w: atlasIndex.
  f16       data2[4]; // xyzw: rotation quaternion.
  f16       data3[4]; // xy: scale.
  f16       data4[4]; // xyzw: color.
} VfxParticleData;

ASSERT(sizeof(VfxParticleData) == 48, "Size needs to match the size defined in glsl");

typedef enum {
  VfxRenderer_AtlasAcquired  = 1 << 0,
  VfxRenderer_AtlasUnloading = 1 << 1,
} VfxRendererFlags;

ecs_comp_define(VfxParticleRendererComp) {
  VfxRendererFlags flags;
  EcsEntityId      atlas;
  EcsEntityId      drawEntity;
};

static EcsEntityId vfx_particle_draw_create(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId   entity = asset_lookup(world, assets, g_vfxParticleGraphic);
  const RendDrawFlags flags  = RendDrawFlags_Preload | RendDrawFlags_SortBackToFront;
  RendDrawComp*       draw   = rend_draw_create(world, entity, flags);
  rend_draw_set_graphic(draw, entity); // Graphic is on the same entity as the draw.
  return entity;
}

ecs_view_define(GlobalView) {
  ecs_access_maybe_write(VfxParticleRendererComp);
  ecs_access_write(AssetManagerComp);
}

ecs_system_define(VfxParticleRendererInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp*        assets   = ecs_view_write_t(globalItr, AssetManagerComp);
  VfxParticleRendererComp* renderer = ecs_view_write_t(globalItr, VfxParticleRendererComp);

  if (!renderer) {
    const EcsEntityId drawEntity = vfx_particle_draw_create(world, assets);
    ecs_world_add_t(
        world,
        ecs_world_global(world),
        VfxParticleRendererComp,
        .atlas      = asset_lookup(world, assets, g_vfxParticleAtlas),
        .drawEntity = drawEntity);
    return;
  }

  if (!(renderer->flags & (VfxRenderer_AtlasAcquired | VfxRenderer_AtlasUnloading))) {
    log_i("Acquiring particle atlas", log_param("id", fmt_text(g_vfxParticleAtlas)));
    asset_acquire(world, renderer->atlas);
    renderer->flags |= VfxRenderer_AtlasAcquired;
  }
}

ecs_system_define(VfxParticleUnloadChangedAtlasSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  VfxParticleRendererComp* renderer = ecs_view_write_t(globalItr, VfxParticleRendererComp);
  if (renderer) {
    const bool isLoaded   = ecs_world_has_t(world, renderer->atlas, AssetLoadedComp);
    const bool isFailed   = ecs_world_has_t(world, renderer->atlas, AssetFailedComp);
    const bool hasChanged = ecs_world_has_t(world, renderer->atlas, AssetChangedComp);

    if (renderer->flags & VfxRenderer_AtlasAcquired && (isLoaded || isFailed) && hasChanged) {
      log_i(
          "Unloading particle atlas",
          log_param("id", fmt_text(g_vfxParticleAtlas)),
          log_param("reason", fmt_text_lit("Asset changed")));

      asset_release(world, renderer->atlas);
      renderer->flags &= ~VfxRenderer_AtlasAcquired;
      renderer->flags |= VfxRenderer_AtlasUnloading;
    }
    if (renderer->flags & VfxRenderer_AtlasUnloading && !isLoaded) {
      renderer->flags &= ~VfxRenderer_AtlasUnloading;
    }
  }
}

ecs_module_init(vfx_particle_module) {
  ecs_register_comp(VfxParticleRendererComp);

  ecs_register_view(GlobalView);

  ecs_register_system(VfxParticleRendererInitSys, ecs_view_id(GlobalView));
  ecs_register_system(VfxParticleUnloadChangedAtlasSys, ecs_view_id(GlobalView));
}

EcsEntityId vfx_particle_atlas(const VfxParticleRendererComp* renderer) { return renderer->atlas; }

EcsEntityId vfx_particle_draw(const VfxParticleRendererComp* renderer) {
  return renderer->drawEntity;
}

void vfx_particle_init(RendDrawComp* draw, const AssetAtlasComp* atlas) {
  const VfxParticleMetaData metaData = {
      .atlasEntriesPerDim    = atlas->entriesPerDim,
      .invAtlasEntriesPerDim = 1.0f / atlas->entriesPerDim,
  };
  rend_draw_set_data(draw, mem_var(metaData));
}

void vfx_particle_output(RendDrawComp* draw, const VfxParticle* particle) {
  VfxParticleData instData;
  instData.data1   = particle->position;
  instData.data1.w = (f32)particle->atlasIndex;
  geo_quat_pack_f16(particle->rotation, instData.data2);
  instData.data3[0] = float_f32_to_f16(particle->sizeX);
  instData.data3[1] = float_f32_to_f16(particle->sizeY);
  geo_color_pack_f16(particle->color, instData.data4);

  const GeoBox bounds =
      geo_box_from_quad(particle->position, particle->sizeX, particle->sizeY, particle->rotation);

  rend_draw_add_instance(draw, mem_var(instData), SceneTags_Vfx, bounds);
}
