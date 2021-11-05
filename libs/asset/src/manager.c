#include "asset_manager.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_dynarray.h"
#include "core_search.h"
#include "ecs_world.h"

typedef struct {
  u32         idHash;
  EcsEntityId asset;
} AssetEntry;

ecs_comp_define(AssetManagerComp) {
  EcsWorld* world;
  DynArray  lookup; // AssetEntry[], kept sorted on the idHash.
};

static void ecs_destruct_manager_comp(void* data) {
  AssetManagerComp* comp = data;
  dynarray_destroy(&comp->lookup);
}

ecs_comp_define(AssetComp) { String id; };

static void ecs_destruct_asset_comp(void* data) {
  AssetComp* comp = data;
  string_free(g_alloc_heap, comp->id);
}

static i8 asset_compare_entry(const void* a, const void* b) {
  return compare_u32(field_ptr(a, AssetEntry, idHash), field_ptr(b, AssetEntry, idHash));
}

static EcsEntityId asset_manager_create(EcsWorld* world) {
  const EcsEntityId entity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world,
      entity,
      AssetManagerComp,
      .world  = world,
      .lookup = dynarray_create_t(g_alloc_heap, AssetEntry, 128));
  return entity;
}

static EcsEntityId asset_entity_create(EcsWorld* world, String id) {
  const EcsEntityId entity = ecs_world_entity_create(world);
  ecs_world_add_t(world, entity, AssetComp, .id = string_dup(g_alloc_heap, id));
  return entity;
}

ecs_module_init(asset_manager_module) {
  ecs_register_comp(AssetManagerComp, .destructor = ecs_destruct_manager_comp);
}

EcsEntityId asset_manager_create_fs(EcsWorld* world, String rootPath) {
  (void)rootPath;
  return asset_manager_create(world);
}

EcsEntityId asset_manager_lookup(AssetManagerComp* manager, String id) {
  /**
   * Find or create an asset entity.
   * Do a binary-search for the first entry with a greater id hash, which means our asset has to be
   * the one before that. If its not that means our asset is not in the lookup and should be
   * inserted at the position of that greater element.
   */
  const u32   idHash  = bits_hash_32(id);
  AssetEntry* begin   = dynarray_at_t(&manager->lookup, 0, AssetEntry);
  AssetEntry* end     = begin + manager->lookup.size;
  AssetEntry* greater = search_binary_greater_t(
      begin, end, AssetEntry, asset_compare_entry, mem_struct(AssetEntry, .idHash = idHash).ptr);
  AssetEntry* tgt = greater ? greater : end;

  // Check if the entry before the greater entry matches the requested id.
  if (tgt > begin && (tgt - 1)->idHash == idHash) {
    return (tgt - 1)->asset; // Existing asset found.
  }

  // No asset found; create a new asset and insert it in the lookup.
  const EcsEntityId newAsset = asset_entity_create(manager->world, id);
  *dynarray_insert_t(&manager->lookup, tgt - begin, AssetEntry) = (AssetEntry){idHash, newAsset};
  return newAsset;
}
