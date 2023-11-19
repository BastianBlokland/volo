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
#define scene_set_member_max 8

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

static void set_storage_clear_all(SetStorage* s) {
  mem_set(array_mem(s->ids), 0);
  array_for_t(s->members, DynArray, arr) { dynarray_clear(arr); }
}

static void set_storage_clear(SetStorage* s, const StringHash set) {
  diag_assert(set);

  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      s->ids[setIdx] = 0;
      dynarray_clear(&s->members[setIdx]);
      return;
    }
  }
}

static bool set_storage_add(SetStorage* s, const StringHash set, const EcsEntityId e) {
  diag_assert(set);

  // Attempt to add it to an existing set.
  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      DynArray* members = &s->members[setIdx];
      *(EcsEntityId*)dynarray_find_or_insert_sorted(members, ecs_compare_entity, &e) = e;
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

static void set_storage_remove(SetStorage* s, const StringHash set, const EcsEntityId e) {
  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      DynArray*          members = &s->members[setIdx];
      const EcsEntityId* itr     = dynarray_search_binary(members, ecs_compare_entity, &e);
      if (itr) {
        const usize index = itr - dynarray_begin_t(members, EcsEntityId);
        dynarray_remove(members, index, 1);
      }
      if (!members->size) {
        s->ids[setIdx] = 0; // Set is now empty; we can free the slot.
      }
      break;
    }
  }
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
    {.setName = string_static("selected"), .tags = SceneTags_Selected},
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

typedef enum {
  SetRequestType_Add,
  SetRequestType_Remove,
  SetRequestType_Clear,
} SetRequestType;

typedef struct {
  SetRequestType type;
  StringHash     set;
  EcsEntityId    target;
} SetRequest;

ecs_comp_define(SceneSetEnvComp) {
  SetStorage* storage;
  DynArray    requests; // SetRequest[]
};

ecs_comp_define(SceneSetMemberComp) { StringHash sets[scene_set_member_max]; };

static void ecs_destruct_set_env_comp(void* data) {
  SceneSetEnvComp* env = data;
  set_storage_destroy(env->storage);
  dynarray_destroy(&env->requests);
}

static bool set_member_contains(const SceneSetMemberComp* member, const StringHash set) {
  for (u32 i = 0; i != array_elems(member->sets); ++i) {
    if (member->sets[i] == set) {
      return true; // Already was part of this set.
    }
  }
  return false;
}

static bool set_member_add(SceneSetMemberComp* member, const StringHash set) {
  if (set_member_contains(member, set)) {
    return true;
  }
  for (u32 i = 0; i != array_elems(member->sets); ++i) {
    if (!member->sets[i]) {
      member->sets[i] = set;
      return true;
    }
  }
  return false;
}

static bool set_member_remove(SceneSetMemberComp* member, const StringHash set) {
  for (u32 i = 0; i != array_elems(member->sets); ++i) {
    if (member->sets[i] == set) {
      member->sets[i] = 0;
      return true;
    }
  }
  return false;
}

static void ecs_combine_set_member(void* dataA, void* dataB) {
  SceneSetMemberComp* compA = dataA;
  SceneSetMemberComp* compB = dataB;

  for (u32 i = 0; i != array_elems(compB->sets); ++i) {
    if (compB->sets[i]) {
      if (UNLIKELY(!set_member_add(compA, compB->sets[i]))) {
        log_e("Set member limit reached", log_param("limit", fmt_int(array_elems(compB->sets))));
      }
    }
  }
}

ecs_view_define(EnvView) { ecs_access_write(SceneSetEnvComp); }

ecs_view_define(MemberView) {
  ecs_access_write(SceneSetMemberComp);
  ecs_access_maybe_write(SceneTagComp);
}

ecs_system_define(SceneSetInitSys) {
  const EcsEntityId global = ecs_world_global(world);
  EcsIterator*      envItr = ecs_view_maybe_at(ecs_world_view_t(world, EnvView), global);
  if (!envItr) {
    ecs_world_add_t(
        world,
        global,
        SceneSetEnvComp,
        .storage  = set_storage_create(g_alloc_heap),
        .requests = dynarray_create_t(g_alloc_heap, SetRequest, 128));
    return;
  }

  SceneSetEnvComp* env = ecs_view_write_t(envItr, SceneSetEnvComp);
  set_storage_clear_all(env->storage);

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

ecs_system_define(SceneSetUpdateSys) {
  const EcsEntityId global = ecs_world_global(world);
  EcsIterator*      envItr = ecs_view_maybe_at(ecs_world_view_t(world, EnvView), global);
  if (!envItr) {
    return;
  }

  EcsView*     memberView = ecs_world_view_t(world, MemberView);
  EcsIterator* itr        = ecs_view_itr(memberView);

  SceneSetEnvComp* env = ecs_view_write_t(envItr, SceneSetEnvComp);
  dynarray_for_t(&env->requests, SetRequest, req) {
    switch (req->type) {
    case SetRequestType_Add:
      if (!ecs_world_exists(world, req->target)) {
        continue;
      }
      if (ecs_view_maybe_jump(itr, req->target)) {
        SceneSetMemberComp* member = ecs_view_write_t(itr, SceneSetMemberComp);
        if (LIKELY(set_member_add(member, req->set))) {
          SceneTagComp* tagComp = ecs_view_write_t(itr, SceneTagComp);
          if (tagComp) {
            tagComp->tags |= set_builtin_tags(req->set);
          }
        } else {
          log_e("Set member limit reached", log_param("limit", fmt_int(array_elems(member->sets))));
        }
      } else {
        ecs_world_add_t(world, req->target, SceneSetMemberComp, .sets[0] = req->set);
      }
      if (UNLIKELY(!set_storage_add(env->storage, req->set, req->target))) {
        log_e("Set limit reached", log_param("limit", fmt_int(scene_set_max)));
      }
      continue;
    case SetRequestType_Remove:
      if (ecs_view_maybe_jump(itr, req->target)) {
        SceneSetMemberComp* member = ecs_view_write_t(itr, SceneSetMemberComp);
        if (set_member_remove(member, req->set)) {
          SceneTagComp* tagComp = ecs_view_write_t(itr, SceneTagComp);
          if (tagComp) {
            tagComp->tags &= ~set_builtin_tags(req->set);
          }
        }
      }
      set_storage_remove(env->storage, req->set, req->target);
      continue;
    case SetRequestType_Clear:
      for (ecs_view_itr_reset(itr); ecs_view_walk(itr);) {
        SceneSetMemberComp* member = ecs_view_write_t(itr, SceneSetMemberComp);
        if (set_member_remove(member, req->set)) {
          SceneTagComp* tagComp = ecs_view_write_t(itr, SceneTagComp);
          if (tagComp) {
            tagComp->tags &= ~set_builtin_tags(req->set);
          }
        }
      }
      set_storage_clear(env->storage, req->set);
      continue;
    }
    diag_crash_msg("Unsupported selection request type");
  }
  dynarray_clear(&env->requests);
}

ecs_module_init(scene_set_module) {
  set_builtin_tags_init();

  ecs_register_comp(SceneSetEnvComp, .destructor = ecs_destruct_set_env_comp);
  ecs_register_comp(SceneSetMemberComp, .combinator = ecs_combine_set_member);

  ecs_register_view(EnvView);
  ecs_register_view(MemberView);

  ecs_register_system(SceneSetInitSys, ecs_view_id(EnvView), ecs_view_id(MemberView));
  ecs_register_system(SceneSetUpdateSys, ecs_view_id(EnvView), ecs_view_id(MemberView));

  ecs_order(SceneSetInitSys, SceneOrder_SetInit);
  ecs_order(SceneSetUpdateSys, SceneOrder_SetUpdate);
}

void scene_set_member_create(
    EcsWorld* world, const EcsEntityId e, const StringHash* sets, const u32 setCount) {
  SceneSetMemberComp* member = ecs_world_add_t(world, e, SceneSetMemberComp);
  for (u32 i = 0; i != setCount; ++i) {
    if (sets[i]) {
      set_member_add(member, sets[i]);
    }
  }
}

bool scene_set_member_contains(const SceneSetMemberComp* member, const StringHash set) {
  return set_member_contains(member, set);
}

bool scene_set_contains(const SceneSetEnvComp* env, const StringHash set, const EcsEntityId e) {
  return set_storage_contains(env->storage, set, e);
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

void scene_set_add(SceneSetEnvComp* env, const StringHash set, const EcsEntityId entity) {
  diag_assert(set);

  *dynarray_push_t(&env->requests, SetRequest) = (SetRequest){
      .type   = SetRequestType_Add,
      .set    = set,
      .target = entity,
  };
}

void scene_set_remove(SceneSetEnvComp* env, const StringHash set, const EcsEntityId entity) {
  diag_assert(set);

  *dynarray_push_t(&env->requests, SetRequest) = (SetRequest){
      .type   = SetRequestType_Remove,
      .set    = set,
      .target = entity,
  };
}

void scene_set_clear(SceneSetEnvComp* env, const StringHash set) {
  diag_assert(set);

  *dynarray_push_t(&env->requests, SetRequest) = (SetRequest){
      .type = SetRequestType_Clear,
      .set  = set,
  };
}
