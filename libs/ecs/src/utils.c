#include "ecs_utils.h"

const void* ecs_utils_read_or_add(EcsWorld* world, const EcsIterator* itr, const EcsCompId comp) {
  const void* res = ecs_view_read(itr, comp);
  return res ? res : ecs_world_comp_add(world, ecs_view_entity(itr), comp, mem_empty);
}

void* ecs_utils_write_or_add(EcsWorld* world, const EcsIterator* itr, const EcsCompId comp) {
  void* res = ecs_view_write(itr, comp);
  return res ? res : ecs_world_comp_add(world, ecs_view_entity(itr), comp, mem_empty);
}
