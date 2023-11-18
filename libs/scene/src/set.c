#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_sentinel.h"
#include "core_thread.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_register.h"
#include "scene_set.h"
#include "scene_tag.h"

#define scene_set_max 64

typedef struct {
  StringHash  ids[scene_set_max];
  DynArray    members[scene_set_max]; // EcsEntityId[][scene_set_max], Entities sorted on their id.
  EcsEntityId mainMembers[scene_set_max];
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
      s->mainMembers[setIdx]                             = e;
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

static u32 set_storage_count(const SetStorage* s, const StringHash set) {
  diag_assert(set);

  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      return (u32)s->members[setIdx].size;
    }
  }
  return 0;
}

static EcsEntityId set_storage_main(const SetStorage* s, const StringHash set) {
  diag_assert(set);

  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      return s->mainMembers[setIdx];
    }
  }
  return 0;
}

static const EcsEntityId* set_storage_begin(const SetStorage* s, const StringHash set) {
  diag_assert(set);

  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      return dynarray_begin_t(&s->members[setIdx], EcsEntityId);
    }
  }
  return null;
}

static const EcsEntityId* set_storage_end(const SetStorage* s, const StringHash set) {
  diag_assert(set);

  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      return dynarray_end_t(&s->members[setIdx], EcsEntityId);
    }
  }
  return null;
}

static void set_storage_update_main_members(SetStorage* s) {
  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (!s->ids[setIdx]) {
      continue; // Unused slot.
    }
    diag_assert(s->members[setIdx].size);
    diag_assert(s->mainMembers[setIdx]);

    if (!dynarray_search_binary(&s->members[setIdx], ecs_compare_entity, &s->mainMembers[setIdx])) {
      // Main-member is no longer in the set; assign a new main-member.
      s->mainMembers[setIdx] = *dynarray_begin_t(&s->members[setIdx], EcsEntityId);
    }
  }
}

static struct {
  String     setName;
  StringHash set;
  SceneTags  tags;
} g_setBuiltinTagEntries[] = {
    {.setName = string_static("unit"), .tags = SceneTags_Unit},
};
static SceneTags g_setBuiltinTags;

static void set_builtin_tags_init_locked() {
  for (u32 i = 0; i != array_elems(g_setBuiltinTagEntries); ++i) {
    g_setBuiltinTagEntries[i].set = string_hash(g_setBuiltinTagEntries[i].setName);
    g_setBuiltinTags |= g_setBuiltinTagEntries[i].tags;
  }
}

static void set_builtin_tags_init() {
  static bool           g_init;
  static ThreadSpinLock g_initLock;
  if (UNLIKELY(!g_init)) {
    thread_spinlock_lock(&g_initLock);
    if (!g_init) {
      set_builtin_tags_init_locked();
      g_init = true;
    }
    thread_spinlock_unlock(&g_initLock);
  }
}

static SceneTags set_builtin_tags(const StringHash set) {
  for (u32 i = 0; i != array_elems(g_setBuiltinTagEntries); ++i) {
    if (g_setBuiltinTagEntries[i].set == set) {
      return g_setBuiltinTagEntries[i].tags;
    }
  }
  return 0;
}

ecs_comp_define(SceneSetEnvComp) { SetStorage* storage; };

ecs_comp_define_public(SceneSetMemberComp);

static void ecs_destruct_set_env_comp(void* data) {
  SceneSetEnvComp* env = data;
  set_storage_destroy(env->storage);
}

ecs_view_define(EnvView) { ecs_access_write(SceneSetEnvComp); }

ecs_view_define(MemberView) {
  ecs_access_read(SceneSetMemberComp);
  ecs_access_maybe_write(SceneTagComp);
}

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
    SceneTagComp*             tagComp    = ecs_view_write_t(itr, SceneTagComp);

    if (tagComp) {
      tagComp->tags &= ~g_setBuiltinTags;
    }

    array_for_t(memberComp->sets, StringHash, setPtr) {
      const StringHash set = *setPtr;
      if (!set) {
        continue; // Empty slot.
      }
      if (UNLIKELY(!set_storage_add(env->storage, set, entity))) {
        log_e("Set limit reached", log_param("limit", fmt_int(scene_set_max)));
        break;
      }
      if (tagComp) {
        tagComp->tags |= set_builtin_tags(set);
      }
    }
  }
  set_storage_update_main_members(env->storage);
}

ecs_module_init(scene_set_module) {
  set_builtin_tags_init();

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

bool scene_set_member_contains(const SceneSetMemberComp* member, const StringHash set) {
  array_for_t(member->sets, StringHash, setPtr) {
    if (*setPtr == set) {
      return true;
    }
  }
  return false;
}

u32 scene_set_count(const SceneSetEnvComp* env, const StringHash set) {
  return set_storage_count(env->storage, set);
}

EcsEntityId scene_set_main(const SceneSetEnvComp* env, const StringHash set) {
  return set_storage_main(env->storage, set);
}

const EcsEntityId* scene_set_begin(const SceneSetEnvComp* env, const StringHash set) {
  return set_storage_begin(env->storage, set);
}

const EcsEntityId* scene_set_end(const SceneSetEnvComp* env, const StringHash set) {
  return set_storage_end(env->storage, set);
}
