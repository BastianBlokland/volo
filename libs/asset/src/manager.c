#include "asset_manager.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_search.h"
#include "ecs_world.h"

#include "repo_internal.h"

typedef struct {
  u32         idHash;
  EcsEntityId asset;
} AssetEntry;

typedef enum {
  AssetFlags_Loading        = 1 << 0,
  AssetFlags_Loaded         = 1 << 1,
  AssetFlags_Active         = AssetFlags_Loading | AssetFlags_Loaded,
  AssetFlags_UpdateRequired = AssetFlags_Loading,
} AssetFlags;

ecs_comp_define(AssetManagerComp) {
  AssetRepo* repo;
  DynArray   lookup; // AssetEntry[], kept sorted on the idHash.
};

ecs_comp_define(AssetComp) {
  String     id;
  u32        refCount;
  AssetFlags flags;
};

ecs_comp_define(AssetLoadedComp);
ecs_comp_define(AssetDirtyComp) { u32 numAcquire, numRelease; };

static void ecs_destruct_manager_comp(void* data) {
  AssetManagerComp* comp = data;
  asset_repo_destroy(comp->repo);
  dynarray_destroy(&comp->lookup);
}

static void ecs_destruct_asset_comp(void* data) {
  AssetComp* comp = data;
  string_free(g_alloc_heap, comp->id);
}

static void ecs_combine_asset_dirty(void* dataA, void* dataB) {
  AssetDirtyComp* compA = dataA;
  AssetDirtyComp* compB = dataB;
  compA->numAcquire += compB->numAcquire;
  compA->numRelease += compB->numRelease;
}

static i8 asset_compare_entry(const void* a, const void* b) {
  return compare_u32(field_ptr(a, AssetEntry, idHash), field_ptr(b, AssetEntry, idHash));
}

static EcsEntityId asset_manager_create_internal(EcsWorld* world, AssetRepo* repo) {
  const EcsEntityId entity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world,
      entity,
      AssetManagerComp,
      .repo   = repo,
      .lookup = dynarray_create_t(g_alloc_heap, AssetEntry, 128));
  return entity;
}

static EcsEntityId asset_entity_create(EcsWorld* world, String id) {
  const EcsEntityId entity = ecs_world_entity_create(world);
  ecs_world_add_t(world, entity, AssetComp, .id = string_dup(g_alloc_heap, id));
  return entity;
}

static void asset_manager_load_start(
    const AssetManagerComp* manager, AssetComp* asset, const EcsEntityId assetEntity) {
  (void)manager;
  (void)asset;
  (void)assetEntity;
}

ecs_view_define(DirtyAssetView) {
  ecs_access_write(AssetComp);
  ecs_access_write(AssetDirtyComp);
};

ecs_view_define(ManagerView) { ecs_access_read(AssetManagerComp); };

static const AssetManagerComp* asset_manager_get(EcsWorld* world) {
  EcsIterator* itr = ecs_view_itr_first(ecs_world_view_t(world, ManagerView));
  return itr ? ecs_view_read_t(itr, AssetManagerComp) : null;
}

ecs_system_define(UpdateDirtyAssetsSys) {
  const AssetManagerComp* manager = asset_manager_get(world);
  if (!manager) {
    /**
     * The manager has not been created yet, we delay the processing of asset requests until a
     * manager has been created.
     * NOTE: No requests get lost, they just stay unprocessed.
     */
    return;
  }

  EcsView* assetsView = ecs_world_view_t(world, DirtyAssetView);
  for (EcsIterator* itr = ecs_view_itr(assetsView); ecs_view_walk(itr);) {
    const EcsEntityId entity    = ecs_view_entity(itr);
    AssetComp*        assetComp = ecs_view_write_t(itr, AssetComp);
    AssetDirtyComp*   dirtyComp = ecs_view_write_t(itr, AssetDirtyComp);

    assetComp->refCount += dirtyComp->numAcquire;
    diag_assert_msg(assetComp->refCount >= dirtyComp->numRelease, "Unbalanced Acquire / Release");
    assetComp->refCount -= dirtyComp->numRelease;

    // Load if the ref-count is non-zero.
    if (assetComp->refCount && !(assetComp->flags & AssetFlags_Active)) {
      asset_manager_load_start(manager, assetComp, entity);
      assetComp->flags |= AssetFlags_Loading;
      goto updateComplete;
    }

    // Check if loading is done.
    if (assetComp->flags & AssetFlags_Loading && ecs_world_has_t(world, entity, AssetLoadedComp)) {
      assetComp->flags &= ~AssetFlags_Loading;
      assetComp->flags |= AssetFlags_Loaded;
    }

    // Unload if the refcount is zero.
    if (!assetComp->refCount && assetComp->flags & AssetFlags_Loaded) {
      ecs_world_remove_t(world, entity, AssetLoadedComp);
      assetComp->flags &= ~AssetFlags_Loaded;
    }

  updateComplete:
    dirtyComp->numAcquire = 0;
    dirtyComp->numRelease = 0;
    if (!(assetComp->flags & AssetFlags_UpdateRequired)) {
      ecs_world_remove_t(world, entity, AssetDirtyComp);
    }
  }
}

ecs_module_init(asset_manager_module) {
  ecs_register_comp(AssetManagerComp, .destructor = ecs_destruct_manager_comp);
  ecs_register_comp(AssetComp, .destructor = ecs_destruct_asset_comp);
  ecs_register_comp_empty(AssetLoadedComp);
  ecs_register_comp(AssetDirtyComp, .combinator = ecs_combine_asset_dirty);

  ecs_register_view(DirtyAssetView);
  ecs_register_view(ManagerView);

  ecs_register_system(UpdateDirtyAssetsSys, ecs_view_id(DirtyAssetView), ecs_view_id(ManagerView));
}

EcsEntityId asset_manager_create_fs(EcsWorld* world, String rootPath) {
  return asset_manager_create_internal(world, asset_repo_create_fs(rootPath));
}

EcsEntityId asset_manager_create_mem(EcsWorld* world, AssetMemRecord* records, usize recordCount) {
  return asset_manager_create_internal(world, asset_repo_create_mem(records, recordCount));
}

EcsEntityId asset_manager_lookup(EcsWorld* world, AssetManagerComp* manager, String id) {
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
  const EcsEntityId newAsset                                    = asset_entity_create(world, id);
  *dynarray_insert_t(&manager->lookup, tgt - begin, AssetEntry) = (AssetEntry){idHash, newAsset};
  return newAsset;
}

void asset_acquire(EcsWorld* world, const EcsEntityId assetEntity) {
  ecs_world_add_t(world, assetEntity, AssetDirtyComp, .numAcquire = 1);
}

void asset_release(EcsWorld* world, const EcsEntityId assetEntity) {
  ecs_world_add_t(world, assetEntity, AssetDirtyComp, .numRelease = 1);
}
