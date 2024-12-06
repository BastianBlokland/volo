#include "ecs_utils.h"
#include "ecs_view.h"

bool ecs_utils_any_raw(EcsView* view) { return ecs_view_first(view) != null; }

const void* ecs_utils_read_first(EcsView* view, const EcsCompId comp) {
  EcsIterator* itr = ecs_view_first(view);
  return itr ? ecs_view_read(itr, comp) : null;
}

void* ecs_utils_write_first(EcsView* view, const EcsCompId comp) {
  EcsIterator* itr = ecs_view_first(view);
  return itr ? ecs_view_write(itr, comp) : null;
}

const void* ecs_utils_read(EcsView* view, const EcsEntityId entity, const EcsCompId comp) {
  EcsIterator* itr = ecs_view_at(view, entity);
  return ecs_view_read(itr, comp);
}

void* ecs_utils_write(EcsView* view, const EcsEntityId entity, const EcsCompId comp) {
  EcsIterator* itr = ecs_view_at(view, entity);
  return ecs_view_write(itr, comp);
}

const void* ecs_utils_read_or_add(EcsWorld* world, const EcsIterator* itr, const EcsCompId comp) {
  const void* res = ecs_view_read(itr, comp);
  return res ? res : ecs_world_add(world, ecs_view_entity(itr), comp, mem_empty);
}

void* ecs_utils_write_or_add(EcsWorld* world, const EcsIterator* itr, const EcsCompId comp) {
  void* res = ecs_view_write(itr, comp);
  return res ? res : ecs_world_add(world, ecs_view_entity(itr), comp, mem_empty);
}

void* ecs_utils_maybe_add(EcsWorld* world, const EcsEntityId entity, const EcsCompId comp) {
  return ecs_world_has(world, entity, comp) ? null : ecs_world_add(world, entity, comp, mem_empty);
}

bool ecs_utils_maybe_remove(EcsWorld* world, const EcsEntityId entity, const EcsCompId comp) {
  if (ecs_world_has(world, entity, comp)) {
    ecs_world_remove(world, entity, comp);
    return true;
  }
  return false;
}
