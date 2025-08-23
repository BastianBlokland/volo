#include "asset/manager.h"
#include "asset/ref.h"
#include "core/stringtable.h"
#include "ecs/entity.h"

EcsEntityId asset_ref_resolve(EcsWorld* world, AssetManagerComp* manager, const AssetRef* ref) {
  if (ecs_entity_valid(ref->entity)) {
    return ref->entity;
  }
  if (!ref->id) {
    return ecs_entity_invalid; // Unset optional asset-ref.
  }
  const String idStr = stringtable_lookup(g_stringtable, ref->id);
  if (UNLIKELY(string_is_empty(idStr))) {
    return ecs_entity_invalid; // String missing from the global string-table.
  }
  return asset_lookup(world, manager, idStr);
}
