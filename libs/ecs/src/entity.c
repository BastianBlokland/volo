#include "core_compare.h"
#include "ecs_entity.h"

i8 ecs_compare_entity(const void* a, const void* b) {
  /**
   * Compare entities by their unique serial.
   */
  const u32 serialA = ecs_entity_id_serial(*((const EcsEntityId*)a));
  const u32 serialB = ecs_entity_id_serial(*((const EcsEntityId*)b));
  return compare_u32(&serialA, &serialB);
}
