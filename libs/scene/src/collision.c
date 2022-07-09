#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "ecs_world.h"
#include "scene_collision.h"
#include "scene_register.h"
#include "scene_transform.h"

typedef struct {
  EcsEntityId entity;
  GeoCapsule  capsule;
} SceneCollisionRegistryEntry;

ecs_comp_define(SceneCollisionRegistryComp) {
  DynArray entries; //  SceneCollisionRegistryEntry[].
};

ecs_comp_define_public(SceneCollisionComp);

static void ecs_destruct_collision_registry_comp(void* data) {
  SceneCollisionRegistryComp* registry = data;
  dynarray_destroy(&registry->entries);
}

ecs_view_define(RegistryUpdateView) { ecs_access_write(SceneCollisionRegistryComp); }

ecs_view_define(CollisionEntityView) {
  ecs_access_read(SceneCollisionComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_read(SceneScaleComp);
}

static SceneCollisionRegistryComp* collision_registry_get_or_create(EcsWorld* world) {
  EcsView*     view = ecs_world_view_t(world, RegistryUpdateView);
  EcsIterator* itr  = ecs_view_maybe_at(view, ecs_world_global(world));
  if (LIKELY(itr)) {
    return ecs_view_write_t(itr, SceneCollisionRegistryComp);
  }
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneCollisionRegistryComp,
      .entries = dynarray_create_t(g_alloc_heap, SceneCollisionRegistryEntry, 512));
}

ecs_system_define(SceneCollisionUpdateSys) {
  SceneCollisionRegistryComp* registry = collision_registry_get_or_create(world);

  dynarray_clear(&registry->entries);

  EcsView* collisionEntities = ecs_world_view_t(world, CollisionEntityView);
  for (EcsIterator* itr = ecs_view_itr(collisionEntities); ecs_view_walk(itr);) {
    const SceneCollisionComp* collision = ecs_view_read_t(itr, SceneCollisionComp);
    const SceneTransformComp* transform = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scale     = ecs_view_read_t(itr, SceneScaleComp);

    switch (collision->type) {
    case SceneCollisionType_Capsule: {
      typedef SceneCollisionRegistryEntry Entry;

      Entry* entry   = dynarray_push_t(&registry->entries, Entry);
      entry->entity  = ecs_view_entity(itr);
      entry->capsule = scene_collision_world_capsule(&collision->data_capsule, transform, scale);
    } break;
    default:
      diag_crash();
    }
  }
}

ecs_module_init(scene_collision_module) {
  ecs_register_comp(SceneCollisionRegistryComp, .destructor = ecs_destruct_collision_registry_comp);
  ecs_register_comp(SceneCollisionComp);

  ecs_register_view(CollisionEntityView);
  ecs_register_view(RegistryUpdateView);

  ecs_register_system(
      SceneCollisionUpdateSys, ecs_view_id(RegistryUpdateView), ecs_view_id(CollisionEntityView));

  ecs_order(SceneCollisionUpdateSys, SceneOrder_CollisionUpdate);
}

void scene_collision_add_capsule(
    EcsWorld* world, const EcsEntityId entity, const SceneCollisionCapsule capsule) {
  ecs_world_add_t(world, entity, SceneCollisionComp, .data_capsule = capsule);
}

GeoCapsule scene_collision_world_capsule(
    const SceneCollisionCapsule* capsule,
    const SceneTransformComp*    transformComp,
    const SceneScaleComp*        scaleComp) {

  const GeoVector basePos   = LIKELY(transformComp) ? transformComp->position : geo_vector(0);
  const GeoQuat   baseRot   = LIKELY(transformComp) ? transformComp->rotation : geo_quat_ident;
  const f32       baseScale = scaleComp ? scaleComp->scale : 1.0f;

  static const GeoVector g_capsuleDir[] = {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}};

  const GeoVector offset = geo_quat_rotate(baseRot, geo_vector_mul(capsule->offset, baseScale));
  const GeoVector dir    = geo_quat_rotate(baseRot, g_capsuleDir[capsule->direction]);

  const GeoVector bottom = geo_vector_add(basePos, offset);
  const GeoVector top    = geo_vector_add(bottom, geo_vector_mul(dir, capsule->height * baseScale));

  return (GeoCapsule){.line = {bottom, top}, .radius = capsule->radius * baseScale};
}
