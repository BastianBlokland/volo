#include "core_alloc.h"
#include "core_array.h"
#include "core_dynarray.h"
#include "core_sentinel.h"
#include "ecs_world.h"
#include "scene_set.h"

#define scene_set_max 64

typedef struct {
  StringHash ids[scene_set_max];
  DynArray   members[scene_set_max]; // EcsEntityId[][scene_set_max], Entities sorted on their id.
} SetStorage;

static SetStorage* set_storage_create(Allocator* alloc) {
  ASSERT(sizeof(SetStorage) <= (usize_kibibyte * 4), "SceneSetStorage has to fit in a page")

  SetStorage* storage = alloc_alloc_t(g_alloc_page, SetStorage);
  mem_set(array_mem(storage->ids), 0);
  array_for_t(storage->members, DynArray, arr) { dynarray_create_t(alloc, EcsEntityId, 0); }
  return storage;
}

static void set_storage_destroy(SetStorage* s) {
  array_for_t(s->members, DynArray, arr) { dynarray_destroy(arr); }
  alloc_free_t(g_alloc_page, s);
}

static bool set_storage_contains(const SetStorage* s, const StringHash set, const EcsEntityId tgt) {
  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      DynArray* setMembers = (DynArray*)&s->members[setIdx];
      return dynarray_search_binary(setMembers, ecs_compare_entity, &tgt) != null;
    }
  }
  return false;
}

const EcsEntityId* set_storage_begin(const SetStorage* s, const StringHash set) {
  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      return dynarray_begin_t(&s->members[setIdx], EcsEntityId);
    }
  }
  return null;
}

const EcsEntityId* set_storage_end(const SetStorage* s, const StringHash set) {
  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      return dynarray_end_t(&s->members[setIdx], EcsEntityId);
    }
  }
  return null;
}

typedef bool (*SetPrunePred)(EcsWorld*, EcsEntityId);

static void set_storage_prune(SetStorage* s, EcsWorld* world, const SetPrunePred pred) {
  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (!s->ids[setIdx]) {
      continue; // Slot unused.
    }
    for (usize memberIdx = s->members[setIdx].size; memberIdx-- > 0;) {
      const EcsEntityId e = dynarray_begin_t(&s->members[memberIdx], EcsEntityId)[memberIdx];
      if (!pred(world, e)) {
        dynarray_remove(&s->members[memberIdx], memberIdx, 1);
      }
    }
    if (!s->members[setIdx].size) {
      s->ids[setIdx] = 0; // Slot is now free.
    }
  }
}

ecs_comp_define(SceneSetEnvComp) { SetStorage* storage; };

ecs_comp_define_public(SceneSetMemberComp);

static void ecs_destruct_set_env_comp(void* data) {
  SceneSetEnvComp* env = data;
  set_storage_destroy(env->storage);
}

ecs_view_define(EnvView) { ecs_access_write(SceneSetEnvComp); }

ecs_system_define(SceneSetUpdateSys) {
  const EcsEntityId global = ecs_world_global(world);
  EcsIterator*      envItr = ecs_view_maybe_at(ecs_world_view_t(world, EnvView), global);
  if (!envItr) {
    ecs_world_add_t(world, global, SceneSetEnvComp, .storage = set_storage_create(g_alloc_heap));
    return;
  }
  SceneSetEnvComp* env = ecs_view_write_t(envItr, SceneSetEnvComp);
  (void)env;
  (void)set_storage_prune;
}

ecs_module_init(scene_set_module) {
  ecs_register_comp(SceneSetEnvComp, .destructor = ecs_destruct_set_env_comp);
  ecs_register_comp(SceneSetMemberComp);

  ecs_register_view(EnvView);

  ecs_register_system(SceneSetUpdateSys, ecs_view_id(EnvView));
}

bool scene_set_contains(const SceneSetEnvComp* env, const StringHash set, const EcsEntityId tgt) {
  return set_storage_contains(env->storage, set, tgt);
}

const EcsEntityId* scene_set_begin(const SceneSetEnvComp* env, const StringHash set) {
  return set_storage_begin(env->storage, set);
}

const EcsEntityId* scene_set_end(const SceneSetEnvComp* env, const StringHash set) {
  return set_storage_end(env->storage, set);
}
