#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_sentinel.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_register.h"
#include "scene_set.h"

#define scene_set_max 64

typedef struct {
  StringHash ids[scene_set_max];
  DynArray   members[scene_set_max]; // EcsEntityId[][scene_set_max], Entities sorted on their id.
} SetStorage;

static SetStorage* set_storage_create(Allocator* alloc) {
  ASSERT(sizeof(SetStorage) <= (usize_kibibyte * 4), "SceneSetStorage has to fit in a page")

  SetStorage* s = alloc_alloc_t(g_alloc_page, SetStorage);
  mem_set(array_mem(s->ids), 0);
  array_for_t(s->members, DynArray, arr) { *arr = dynarray_create_t(alloc, EcsEntityId, 0); }
  return s;
}

static void set_storage_destroy(SetStorage* s) {
  array_for_t(s->members, DynArray, arr) { dynarray_destroy(arr); }
  alloc_free_t(g_alloc_page, s);
}

static void set_storage_clear(SetStorage* s) {
  mem_set(array_mem(s->ids), 0);
  array_for_t(s->members, DynArray, arr) { dynarray_clear(arr); }
}

static bool set_storage_add(SetStorage* s, const StringHash set, const EcsEntityId e) {
  diag_assert(set);

  // Attempt to add it to an existing set.
  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      *dynarray_insert_sorted_t(&s->members[setIdx], EcsEntityId, ecs_compare_entity, &e) = e;
      return true;
    }
  }
  // Attempt to add a new set.
  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (!s->ids[setIdx]) {
      s->ids[setIdx]                                     = set;
      *dynarray_push_t(&s->members[setIdx], EcsEntityId) = e;
      return true;
    }
  }
  // No more space for this set.
  return false;
}

static bool set_storage_contains(const SetStorage* s, const StringHash set, const EcsEntityId e) {
  diag_assert(set);

  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      return dynarray_search_binary((DynArray*)&s->members[setIdx], ecs_compare_entity, &e) != null;
    }
  }
  return false;
}

const EcsEntityId* set_storage_begin(const SetStorage* s, const StringHash set) {
  diag_assert(set);

  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      return dynarray_begin_t(&s->members[setIdx], EcsEntityId);
    }
  }
  return null;
}

const EcsEntityId* set_storage_end(const SetStorage* s, const StringHash set) {
  diag_assert(set);

  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      return dynarray_end_t(&s->members[setIdx], EcsEntityId);
    }
  }
  return null;
}

ecs_comp_define(SceneSetEnvComp) { SetStorage* storage; };

ecs_comp_define_public(SceneSetMemberComp);

static void ecs_destruct_set_env_comp(void* data) {
  SceneSetEnvComp* env = data;
  set_storage_destroy(env->storage);
}

ecs_view_define(EnvView) { ecs_access_write(SceneSetEnvComp); }
ecs_view_define(MemberView) { ecs_access_read(SceneSetMemberComp); }

ecs_system_define(SceneSetInitSys) {
  const EcsEntityId global = ecs_world_global(world);
  EcsIterator*      envItr = ecs_view_maybe_at(ecs_world_view_t(world, EnvView), global);
  if (!envItr) {
    ecs_world_add_t(world, global, SceneSetEnvComp, .storage = set_storage_create(g_alloc_heap));
    return;
  }

  SceneSetEnvComp* env = ecs_view_write_t(envItr, SceneSetEnvComp);
  set_storage_clear(env->storage);

  EcsView* memberView = ecs_world_view_t(world, MemberView);
  for (EcsIterator* itr = ecs_view_itr(memberView); ecs_view_walk(itr);) {
    const EcsEntityId         entity     = ecs_view_entity(itr);
    const SceneSetMemberComp* memberComp = ecs_view_read_t(itr, SceneSetMemberComp);

    array_for_t(memberComp->sets, StringHash, setPtr) {
      const StringHash set = *setPtr;
      if (!set) {
        continue; // Empty slot.
      }
      if (UNLIKELY(!set_storage_add(env->storage, set, entity))) {
        log_e("Set limit reached", log_param("limit", fmt_int(scene_set_max)));
        break;
      }
    }
  }
}

ecs_module_init(scene_set_module) {
  ecs_register_comp(SceneSetEnvComp, .destructor = ecs_destruct_set_env_comp);
  ecs_register_comp(SceneSetMemberComp);

  ecs_register_view(EnvView);
  ecs_register_view(MemberView);

  ecs_register_system(SceneSetInitSys, ecs_view_id(EnvView), ecs_view_id(MemberView));

  ecs_order(SceneSetInitSys, SceneOrder_SetInit);
}

bool scene_set_contains(const SceneSetEnvComp* env, const StringHash set, const EcsEntityId e) {
  return set_storage_contains(env->storage, set, e);
}

const EcsEntityId* scene_set_begin(const SceneSetEnvComp* env, const StringHash set) {
  return set_storage_begin(env->storage, set);
}

const EcsEntityId* scene_set_end(const SceneSetEnvComp* env, const StringHash set) {
  return set_storage_end(env->storage, set);
}
