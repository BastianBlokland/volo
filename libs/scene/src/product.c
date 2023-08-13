#include "asset_manager.h"
#include "asset_prefab.h"
#include "asset_product.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_faction.h"
#include "scene_lifetime.h"
#include "scene_nav.h"
#include "scene_prefab.h"
#include "scene_product.h"
#include "scene_renderable.h"
#include "scene_sound.h"
#include "scene_time.h"
#include "scene_transform.h"

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

ecs_comp_define(SceneProductPreviewComp) { EcsEntityId instigator; };

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

static GeoVector product_world_on_nav(const SceneNavEnvComp* nav, const GeoVector pos) {
  GeoNavCell cell = scene_nav_at_position(nav, pos);
  scene_nav_closest_unblocked_n(nav, cell, (GeoNavCellContainer){.cells = &cell, .capacity = 1});
  return scene_nav_position(nav, cell);
}

static void product_sound_play(EcsWorld* world, const EcsEntityId soundAsset, const f32 gain) {
  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, SceneLifetimeDurationComp, .duration = time_second);
  ecs_world_add_t(world, e, SceneSoundComp, .asset = soundAsset, .gain = gain, .pitch = 1.0f);
}

static GeoVector product_world_from_local(EcsIterator* itr, const GeoVector localPos) {
  const SceneTransformComp* transComp = ecs_view_read_t(itr, SceneTransformComp);
  const SceneScaleComp*     scaleComp = ecs_view_read_t(itr, SceneScaleComp);

  const GeoVector position = transComp ? transComp->position : geo_vector(0);
  const GeoQuat   rotation = transComp ? transComp->rotation : geo_quat_ident;
  const f32       scale    = scaleComp ? scaleComp->scale : 1.0f;

  return geo_vector_add(position, geo_quat_rotate(rotation, geo_vector_mul(localPos, scale)));
}

static GeoVector product_spawn_pos(EcsIterator* itr, const SceneNavEnvComp* nav) {
  const SceneProductionComp* production = ecs_view_read_t(itr, SceneProductionComp);
  const GeoVector            pos        = product_world_from_local(itr, production->spawnPos);
  return product_world_on_nav(nav, pos);
}

static GeoVector product_rally_pos(EcsIterator* itr) {
  const SceneProductionComp* production = ecs_view_read_t(itr, SceneProductionComp);
  if (production->flags & SceneProductFlags_RallyLocalSpace) {
    return product_world_from_local(itr, production->rallyPos);
  }
  return production->rallyPos;
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

  for (u16 i = 0; i != production->queueCount; ++i) {
    production->queues[i] = (SceneProductQueue){
        .product = &map->products[productSet->productIndex + i],
    };
  }

  return true;
}

ecs_view_define(ProductMapView) { ecs_access_read(AssetProductMapComp); }

ecs_view_define(ProductionView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_write(SceneProductionComp);
}

ecs_view_define(PrefabMapView) { ecs_access_read(AssetPrefabMapComp); }

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

ecs_view_define(UpdateGlobalView) {
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(ScenePrefabResourceComp);
  ecs_access_read(SceneProductResourceComp);
  ecs_access_read(SceneTimeComp);
}

typedef struct {
  EcsWorld*                 world;
  const SceneNavEnvComp*    nav;
  const AssetPrefabMapComp* prefabMap;
  SceneProductionComp*      production;
  SceneProductQueue*        queue;
  EcsIterator*              itr;
  bool                      anyQueueBusy;
  TimeDuration              timeDelta;
} ProductQueueContext;

static bool product_queue_any_busy(ProductQueueContext* ctx) {
  for (u16 queueIndex = 0; queueIndex != ctx->production->queueCount; ++queueIndex) {
    SceneProductQueue* queue = &ctx->production->queues[queueIndex];
    if (queue->state > SceneProductState_Idle) {
      return true;
    }
  }
  return false;
}

static void product_queue_process_requests(ProductQueueContext* ctx) {
  SceneProductQueue*  queue   = ctx->queue;
  const AssetProduct* product = ctx->queue->product;
  if (queue->requests & SceneProductRequest_EnqueueSingle && queue->count < product->queueMax) {
    ++queue->count;
  }
  if (queue->requests & SceneProductRequest_EnqueueBulk && queue->count < product->queueMax) {
    queue->count += math_min(product->queueBulkSize, product->queueMax - queue->count);
  }
  if (queue->requests & SceneProductRequest_CancelSingle && queue->count) {
    --queue->count;
  }
  if (queue->requests & SceneProductRequest_CancelAll) {
    queue->count = 0;
  }
}

typedef enum {
  ProductResult_Running,
  ProductResult_Success,
  ProductResult_Cancelled,
} ProductResult;

static ProductResult product_queue_process_ready(ProductQueueContext* ctx) {
  switch (ctx->queue->product->type) {
  case AssetProduct_Unit:
    return ProductResult_Success;
  case AssetProduct_Placable:
    if (ctx->queue->requests & SceneProductRequest_Activate) {
      return ProductResult_Success;
    }
    return ProductResult_Running;
  }
  UNREACHABLE
}

static ProductResult product_queue_process_active_unit(ProductQueueContext* ctx) {
  const AssetProduct* product = ctx->queue->product;
  diag_assert(product->type == AssetProduct_Unit);

  const SceneFactionComp* factionComp = ecs_view_read_t(ctx->itr, SceneFactionComp);

  const u32        spawnCount = product->data_unit.unitCount;
  const GeoVector  spawnPos   = product_spawn_pos(ctx->itr, ctx->nav);
  const GeoVector  rallyPos   = product_rally_pos(ctx->itr);
  const GeoNavCell rallyCell  = scene_nav_at_position(ctx->nav, rallyPos);

  GeoNavCell                targetCells[32];
  const GeoNavCellContainer targetCellContainer = {
      .cells    = targetCells,
      .capacity = math_min(spawnCount, array_elems(targetCells)),
  };
  const u32 targetCellCnt = scene_nav_closest_unblocked_n(ctx->nav, rallyCell, targetCellContainer);

  const GeoVector toRallyVec = geo_vector_sub(rallyPos, spawnPos);
  const f32       toRallyMag = geo_vector_mag(toRallyVec);
  const GeoVector forward =
      toRallyMag > f32_epsilon ? geo_vector_div(toRallyVec, toRallyMag) : geo_forward;

  for (u32 i = 0; i != spawnCount; ++i) {
    const EcsEntityId e = scene_prefab_spawn(
        ctx->world,
        &(ScenePrefabSpec){
            .prefabId = product->data_unit.unitPrefab,
            .position = spawnPos,
            .rotation = geo_quat_look(forward, geo_up),
            .scale    = 1.0f,
            .faction  = factionComp ? factionComp->id : SceneFaction_None,
        });
    GeoVector pos;
    if (LIKELY(i < targetCellCnt)) {
      const bool sameCellAsRallPos = targetCells[i].data == rallyCell.data;
      pos = sameCellAsRallPos ? rallyPos : scene_nav_position(ctx->nav, targetCells[i]);
    } else {
      // We didn't find a unblocked cell for this entity; just move to the raw rallyPos.
      pos = rallyPos;
    }
    ecs_world_add_t(ctx->world, e, SceneNavRequestComp, .targetPos = pos);
  }
  return ProductResult_Success;
}

static EcsEntityId product_placement_preview_create(ProductQueueContext* ctx) {
  diag_assert(ctx->queue->product->type == AssetProduct_Placable);
  const EcsEntityId instigator = ecs_view_entity(ctx->itr);

  const EcsEntityId e = ecs_world_entity_create(ctx->world);
  ecs_world_add_t(ctx->world, e, SceneProductPreviewComp, .instigator = instigator);
  ecs_world_add_t(
      ctx->world,
      e,
      SceneTransformComp,
      .position = ctx->production->placementPos,
      .rotation = geo_quat_ident);

  const StringHash   prefabId = ctx->queue->product->data_placable.prefab;
  const AssetPrefab* prefab   = asset_prefab_get(ctx->prefabMap, prefabId);
  if (prefab) {
    const AssetPrefabTrait* renderableTrait =
        asset_prefab_trait_get(ctx->prefabMap, prefab, AssetPrefabTrait_Renderable);
    if (renderableTrait) {
      const EcsEntityId graphic = renderableTrait->data_renderable.graphic;
      ecs_world_add_t(ctx->world, e, SceneRenderableComp, .graphic = graphic, .alpha = 0.5f);
    }
  }
  return e;
}

static ProductResult product_queue_process_active_placeable(ProductQueueContext* ctx) {
  if (ctx->queue->requests & SceneProductRequest_PlacementAccept) {
    return ProductResult_Success;
  }
  if (ctx->queue->requests & SceneProductRequest_PlacementCancel) {
    return ProductResult_Cancelled;
  }
  if (!ctx->production->placementPreview) {
    ctx->production->placementPreview = product_placement_preview_create(ctx);
  }
  return ProductResult_Running;
}

static ProductResult product_queue_process_active(ProductQueueContext* ctx) {
  switch (ctx->queue->product->type) {
  case AssetProduct_Unit:
    return product_queue_process_active_unit(ctx);
  case AssetProduct_Placable:
    return product_queue_process_active_placeable(ctx);
  }
  UNREACHABLE
}

static void product_queue_update(ProductQueueContext* ctx) {
  SceneProductQueue*  queue   = ctx->queue;
  const AssetProduct* product = ctx->queue->product;
  ProductResult       result;
  switch (queue->state) {
  case SceneProductState_Idle:
    if (queue->count && !ctx->anyQueueBusy) {
      queue->state      = SceneProductState_Building;
      queue->progress   = 0.0f;
      ctx->anyQueueBusy = true;
    }
    break;
  case SceneProductState_Building:
    if (!queue->count) {
      queue->state    = SceneProductState_Idle;
      queue->progress = 0.0f;
      break;
    }
    queue->progress += (f32)ctx->timeDelta / (f32)queue->product->costTime;
    if (queue->progress >= 1.0f) {
      queue->state    = SceneProductState_Ready;
      queue->progress = 0.0f;
      if (product->soundReady) {
        product_sound_play(ctx->world, product->soundReady, product->soundReadyGain);
      }
      // Fallthrough.
    } else {
      break;
    }
  case SceneProductState_Ready:
    if (!queue->count) {
      queue->state    = SceneProductState_Idle;
      queue->progress = 0.0f;
      break;
    }
    result = product_queue_process_ready(ctx);
    if (result == ProductResult_Success) {
      queue->state = SceneProductState_Active;
      // Fallthrough.
    } else {
      break;
    }
  case SceneProductState_Active:
    if (!queue->count) {
      queue->state    = SceneProductState_Idle;
      queue->progress = 0.0f;
      break;
    }
    result = product_queue_process_active(ctx);
    if (result == ProductResult_Cancelled) {
      queue->state = SceneProductState_Ready;
      break;
    } else if (result == ProductResult_Success) {
      --queue->count;
      queue->state = SceneProductState_Cooldown;
      // Fallthrough.
    } else {
      break;
    }
  case SceneProductState_Cooldown:
    queue->progress += (f32)ctx->timeDelta / (f32)queue->product->cooldown;
    if (queue->progress >= 1.0f) {
      queue->progress = 0.0f;
      if (queue->count) {
        queue->state = SceneProductState_Building;
      } else {
        queue->state = SceneProductState_Idle;
      }
    }
    break;
  }
}

ecs_system_define(SceneProductUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp*           time      = ecs_view_read_t(globalItr, SceneTimeComp);
  const SceneNavEnvComp*         nav       = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const ScenePrefabResourceComp* prefabRes = ecs_view_read_t(globalItr, ScenePrefabResourceComp);

  EcsView*                   productMapView = ecs_world_view_t(world, ProductMapView);
  const AssetProductMapComp* productMap     = product_map_get(globalItr, productMapView);
  if (!productMap) {
    return;
  }

  EcsView*     prefabMapView = ecs_world_view_t(world, PrefabMapView);
  EcsIterator* prefabMapItr  = ecs_view_maybe_at(prefabMapView, scene_prefab_map(prefabRes));
  if (!prefabMapItr) {
    return;
  }
  const AssetPrefabMapComp* prefabMap = ecs_view_read_t(prefabMapItr, AssetPrefabMapComp);

  EcsView* productionView = ecs_world_view_t(world, ProductionView);
  for (EcsIterator* itr = ecs_view_itr(productionView); ecs_view_walk(itr);) {
    SceneProductionComp* production = ecs_view_write_t(itr, SceneProductionComp);

    // Initialize product queues.
    if (!production->queues && !product_queues_init(production, productMap)) {
      continue;
    }

    ProductQueueContext ctx = {
        .world      = world,
        .nav        = nav,
        .prefabMap  = prefabMap,
        .production = production,
        .itr        = itr,
        .timeDelta  = time->delta,
    };
    ctx.anyQueueBusy = product_queue_any_busy(&ctx);

    // Update product queues.
    for (u16 queueIndex = 0; queueIndex != production->queueCount; ++queueIndex) {
      ctx.queue = &production->queues[queueIndex];
      product_queue_process_requests(&ctx);
      product_queue_update(&ctx);
      ctx.queue->requests = 0;
    }
  }
}

ecs_view_define(PreviewUpdateView) {
  ecs_access_read(SceneProductPreviewComp);
  ecs_access_write(SceneTransformComp);
}

ecs_view_define(PreviewInstigatorView) { ecs_access_read(SceneProductionComp); }

ecs_system_define(SceneProductPreviewUpdateSys) {
  EcsView* previewView    = ecs_world_view_t(world, PreviewUpdateView);
  EcsView* instigatorView = ecs_world_view_t(world, PreviewInstigatorView);

  EcsIterator* instigatorItr = ecs_view_itr(instigatorView);

  for (EcsIterator* itr = ecs_view_itr(previewView); ecs_view_walk(itr);) {
    const SceneProductPreviewComp* preview = ecs_view_read_t(itr, SceneProductPreviewComp);
    SceneTransformComp*            trans   = ecs_view_write_t(itr, SceneTransformComp);
    if (!ecs_view_maybe_jump(instigatorItr, preview->instigator)) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
      continue;
    }
    const SceneProductionComp* production = ecs_view_read_t(instigatorItr, SceneProductionComp);
    trans->position                       = production->placementPos;
  }
}

ecs_module_init(scene_product_module) {
  ecs_register_comp(SceneProductResourceComp, .destructor = ecs_destruct_product_resource);
  ecs_register_comp(SceneProductionComp, .destructor = ecs_destruct_production);
  ecs_register_comp(SceneProductPreviewComp);

  ecs_register_view(ProductMapView);
  ecs_register_view(ProductionView);
  ecs_register_view(PrefabMapView);

  ecs_register_system(SceneProductResInitSys, ecs_register_view(ResInitGlobalView));

  ecs_register_system(
      SceneProductResUnloadChangedSys,
      ecs_register_view(ResUnloadGlobalView),
      ecs_view_id(ProductionView));

  ecs_register_system(
      SceneProductUpdateSys,
      ecs_register_view(UpdateGlobalView),
      ecs_view_id(ProductionView),
      ecs_view_id(ProductMapView),
      ecs_view_id(PrefabMapView));

  ecs_register_system(
      SceneProductPreviewUpdateSys,
      ecs_register_view(PreviewUpdateView),
      ecs_register_view(PreviewInstigatorView));
}

void scene_product_init(EcsWorld* world, const String productMapId) {
  diag_assert_msg(productMapId.size, "Invalid productMapId");

  ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneProductResourceComp,
      .mapId = string_dup(g_alloc_heap, productMapId));
}

bool scene_product_placement_active(const SceneProductionComp* production) {
  for (u16 queueIndex = 0; queueIndex != production->queueCount; ++queueIndex) {
    SceneProductQueue* queue = &production->queues[queueIndex];
    if (queue->product->type == AssetProduct_Placable && queue->state == SceneProductState_Active) {
      return true;
    }
  }
  return false;
}

void scene_product_placement_accept(SceneProductionComp* production) {
  for (u16 queueIndex = 0; queueIndex != production->queueCount; ++queueIndex) {
    SceneProductQueue* queue = &production->queues[queueIndex];
    if (queue->product->type == AssetProduct_Placable && queue->state == SceneProductState_Active) {
      queue->requests |= SceneProductRequest_PlacementAccept;
    }
  }
}

void scene_product_placement_cancel(SceneProductionComp* production) {
  for (u16 queueIndex = 0; queueIndex != production->queueCount; ++queueIndex) {
    SceneProductQueue* queue = &production->queues[queueIndex];
    if (queue->product->type == AssetProduct_Placable && queue->state == SceneProductState_Active) {
      queue->requests |= SceneProductRequest_PlacementCancel;
    }
  }
}
