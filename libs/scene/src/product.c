#include "asset_manager.h"
#include "asset_product.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_product.h"

typedef enum {
  ProductRes_MapAcquired  = 1 << 0,
  ProductRes_MapUnloading = 1 << 1,
} ProductResFlags;

ecs_comp_define(SceneProductResourceComp) {
  ProductResFlags flags;
  String          mapId;
  EcsEntityId     mapEntity;
};

ecs_comp_define_public(SceneProductionComp);

static void ecs_destruct_product_resource(void* data) {
  SceneProductResourceComp* comp = data;
  string_free(g_alloc_heap, comp->mapId);
}

static void ecs_destruct_production(void* data) {
  SceneProductionComp* comp = data;
  if (comp->queues) {
    alloc_free_array_t(g_alloc_heap, comp->queues, comp->queueCount);
  }
}

static const AssetProductMapComp* product_map_get(EcsIterator* globalItr, EcsView* mapView) {
  const SceneProductResourceComp* resource = ecs_view_read_t(globalItr, SceneProductResourceComp);
  if (!(resource->flags & ProductRes_MapAcquired)) {
    return null;
  }
  EcsIterator* itr = ecs_view_maybe_at(mapView, resource->mapEntity);
  return itr ? ecs_view_read_t(itr, AssetProductMapComp) : null;
}

static bool product_queues_init(SceneProductionComp* production, const AssetProductMapComp* map) {
  diag_assert(!production->queues);
  diag_assert(production->productSetId);

  const AssetProductSet* productSet = asset_productset_get(map, production->productSetId);
  if (!productSet) {
    log_e("Product set not found", log_param("product-set-id-hash", production->productSetId));
    return false;
  }

  diag_assert(productSet->productCount);
  production->queues     = alloc_array_t(g_alloc_heap, SceneProductQueue, productSet->productCount);
  production->queueCount = productSet->productCount;

  for (u32 i = 0; i != production->queueCount; ++i) {
    production->queues[i] = (SceneProductQueue){
        .product = &map->products[productSet->productIndex + i],
    };
  }

  return true;
}

ecs_view_define(ProductMapView) { ecs_access_read(AssetProductMapComp); }
ecs_view_define(ProductionView) { ecs_access_write(SceneProductionComp); }

ecs_view_define(ResInitGlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_write(SceneProductResourceComp);
}

ecs_system_define(SceneProductResInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, ResInitGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp*         assets   = ecs_view_write_t(globalItr, AssetManagerComp);
  SceneProductResourceComp* resource = ecs_view_write_t(globalItr, SceneProductResourceComp);

  if (!resource->mapEntity) {
    resource->mapEntity = asset_lookup(world, assets, resource->mapId);
  }

  if (!(resource->flags & (ProductRes_MapAcquired | ProductRes_MapUnloading))) {
    log_i("Acquiring product-map", log_param("id", fmt_text(resource->mapId)));
    asset_acquire(world, resource->mapEntity);
    resource->flags |= ProductRes_MapAcquired;
  }
}

static void scene_production_reset_all_queues(EcsWorld* world) {
  EcsView* view = ecs_world_view_t(world, ProductionView);
  for (EcsIterator* itr = ecs_view_itr(view); ecs_view_walk(itr);) {
    SceneProductionComp* prod = ecs_view_write_t(itr, SceneProductionComp);
    if (prod->queues) {
      alloc_free_array_t(g_alloc_heap, prod->queues, prod->queueCount);
      prod->queues     = null;
      prod->queueCount = 0;
    }
  }
}

ecs_view_define(ResUnloadGlobalView) { ecs_access_write(SceneProductResourceComp); }

ecs_system_define(SceneProductResUnloadChangedSys) {
  EcsView*     globalView = ecs_world_view_t(world, ResUnloadGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneProductResourceComp* resource = ecs_view_write_t(globalItr, SceneProductResourceComp);
  if (!ecs_entity_valid(resource->mapEntity)) {
    return;
  }
  const bool isLoaded   = ecs_world_has_t(world, resource->mapEntity, AssetLoadedComp);
  const bool isFailed   = ecs_world_has_t(world, resource->mapEntity, AssetFailedComp);
  const bool hasChanged = ecs_world_has_t(world, resource->mapEntity, AssetChangedComp);

  if (resource->flags & ProductRes_MapAcquired && (isLoaded || isFailed) && hasChanged) {
    log_i(
        "Unloading product-map",
        log_param("id", fmt_text(resource->mapId)),
        log_param("reason", fmt_text_lit("Asset changed")));

    asset_release(world, resource->mapEntity);
    resource->flags &= ~ProductRes_MapAcquired;
    resource->flags |= ProductRes_MapUnloading;

    /**
     * Reset all queues so they will be re-initialized using the new product map.
     * TODO: Instead of throwing away all queue state we can try to preserve the old state when
     * its still compatible with the new product-map.
     */
    scene_production_reset_all_queues(world);
  }
  if (resource->flags & ProductRes_MapUnloading && !isLoaded) {
    resource->flags &= ~ProductRes_MapUnloading;
  }
}

ecs_view_define(UpdateGlobalView) { ecs_access_read(SceneProductResourceComp); }

ecs_system_define(SceneProductUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  EcsView*                   productMapView = ecs_world_view_t(world, ProductMapView);
  const AssetProductMapComp* productMap     = product_map_get(globalItr, productMapView);
  if (!productMap) {
    return;
  }

  EcsView* productionView = ecs_world_view_t(world, ProductionView);
  for (EcsIterator* itr = ecs_view_itr(productionView); ecs_view_walk(itr);) {
    SceneProductionComp* production = ecs_view_write_t(itr, SceneProductionComp);

    // Initialize product queues.
    if (!production->queues && !product_queues_init(production, productMap)) {
      continue;
    }
  }
}

ecs_module_init(scene_product_module) {
  ecs_register_comp(SceneProductResourceComp, .destructor = ecs_destruct_product_resource);
  ecs_register_comp(SceneProductionComp, .destructor = ecs_destruct_production);

  ecs_register_view(ProductMapView);
  ecs_register_view(ProductionView);

  ecs_register_system(SceneProductResInitSys, ecs_register_view(ResInitGlobalView));

  ecs_register_system(
      SceneProductResUnloadChangedSys,
      ecs_register_view(ResUnloadGlobalView),
      ecs_view_id(ProductionView));

  ecs_register_system(
      SceneProductUpdateSys,
      ecs_register_view(UpdateGlobalView),
      ecs_view_id(ProductionView),
      ecs_view_id(ProductMapView));
}

void scene_product_init(EcsWorld* world, const String productMapId) {
  diag_assert_msg(productMapId.size, "Invalid productMapId");

  ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneProductResourceComp,
      .mapId = string_dup(g_alloc_heap, productMapId));
}

EcsEntityId scene_product_map(const SceneProductResourceComp* comp) { return comp->mapEntity; }
