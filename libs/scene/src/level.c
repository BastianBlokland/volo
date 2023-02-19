#include "asset_level.h"
#include "asset_manager.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_faction.h"
#include "scene_prefab.h"
#include "scene_transform.h"

ASSERT(AssetLevelFaction_Count == SceneFaction_Count, "Mismatching faction counts");
ASSERT(AssetLevelFaction_None == SceneFaction_None, "Mismatching faction sentinel");

typedef enum {
  SceneLevelRequestType_Save,
} SceneLevelRequestType;

ecs_comp_define(SceneLevelRequestComp) {
  SceneLevelRequestType type;
  String                levelId;
};

static void ecs_destruct_level_request(void* data) {
  SceneLevelRequestComp* comp = data;
  string_free(g_alloc_heap, comp->levelId);
}

ecs_view_define(GlobalView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(RequestView) { ecs_access_read(SceneLevelRequestComp); }
ecs_view_define(InstanceView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_read(ScenePrefabInstanceComp);
}

static void scene_level_object_push(
    DynArray*    levelObjects, // AssetLevelObject[]
    EcsIterator* instanceItr) {

  const ScenePrefabInstanceComp* prefabInst = ecs_view_read_t(instanceItr, ScenePrefabInstanceComp);
  const SceneTransformComp*      maybeTrans = ecs_view_read_t(instanceItr, SceneTransformComp);
  const SceneFactionComp*        maybeFaction = ecs_view_read_t(instanceItr, SceneFactionComp);

  const String prefabName = stringtable_lookup(g_stringtable, prefabInst->prefabId);
  if (string_is_empty(prefabName)) {
    log_w("Prefab name not found", log_param("prefab-id", fmt_int(prefabInst->prefabId)));
    return;
  }

  const GeoQuat   rot         = maybeTrans ? maybeTrans->rotation : geo_quat_ident;
  const GeoVector rotEulerDeg = geo_vector_mul(geo_quat_to_euler(rot), math_rad_to_deg);

  *dynarray_push_t(levelObjects, AssetLevelObject) = (AssetLevelObject){
      .prefab   = prefabName,
      .position = maybeTrans ? maybeTrans->position : geo_vector(0),
      .rotation = rotEulerDeg,
      .faction  = (AssetLevelFaction)(maybeFaction ? maybeFaction->id : SceneFaction_None),
  };
}

static void scene_level_process_save(AssetManagerComp* assets, const String id, EcsView* instView) {
  DynArray levelObjects = dynarray_create_t(g_alloc_heap, AssetLevelObject, 1024);
  for (EcsIterator* itr = ecs_view_itr(instView); ecs_view_walk(itr);) {
    scene_level_object_push(&levelObjects, itr);
  }

  const AssetLevel level = {
      .objects.values = dynarray_begin_t(&levelObjects, AssetLevelObject),
      .objects.count  = levelObjects.size,
  };
  asset_level_save(assets, id, level);

  log_i(
      "Level saved",
      log_param("id", fmt_text(id)),
      log_param("objects", fmt_int(levelObjects.size)));

  dynarray_destroy(&levelObjects);
}

ecs_system_define(SceneLevelRequestsSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  EcsView* requestView  = ecs_world_view_t(world, RequestView);
  EcsView* instanceView = ecs_world_view_t(world, InstanceView);

  for (EcsIterator* itr = ecs_view_itr(requestView); ecs_view_walk(itr);) {
    const EcsEntityId            requestEntity = ecs_view_entity(itr);
    const SceneLevelRequestComp* request       = ecs_view_read_t(itr, SceneLevelRequestComp);

    switch (request->type) {
    case SceneLevelRequestType_Save:
      scene_level_process_save(assets, request->levelId, instanceView);
      break;
    }

    ecs_world_entity_destroy(world, requestEntity);
  }
}

ecs_module_init(scene_level_module) {
  ecs_register_comp(SceneLevelRequestComp, .destructor = ecs_destruct_level_request);

  ecs_register_view(GlobalView);
  ecs_register_view(RequestView);
  ecs_register_view(InstanceView);

  ecs_register_system(
      SceneLevelRequestsSys,
      ecs_view_id(GlobalView),
      ecs_view_id(RequestView),
      ecs_view_id(InstanceView));
}

void scene_level_save(EcsWorld* world, const String levelId) {
  diag_assert(!string_is_empty(levelId));

  const EcsEntityId requestEntity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world,
      requestEntity,
      SceneLevelRequestComp,
      .type    = SceneLevelRequestType_Save,
      .levelId = string_dup(g_alloc_heap, levelId));
}
