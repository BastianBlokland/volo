#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_intrinsic.h"
#include "core_sentinel.h"
#include "core_stringtable.h"
#include "core_thread.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_register.h"
#include "scene_set.h"
#include "scene_tag.h"

#ifdef VOLO_SIMD
#include "core_simd.h"
#endif

#define scene_set_wellknown_names 1
#define scene_set_max 64

typedef struct {
  ALIGNAS(16) StringHash ids[scene_set_max];
  DynArray    members[scene_set_max]; // EcsEntityId[][scene_set_max], Entities sorted on their id.
  EcsEntityId mainMembers[scene_set_max];
} SetStorage;

static SetStorage* set_storage_create(Allocator* alloc) {
  ASSERT(sizeof(SetStorage) <= (usize_kibibyte * 4), "SceneSetStorage has to fit in a page")

  SetStorage* s = alloc_alloc_t(g_allocHeap, SetStorage);
  mem_set(array_mem(s->ids), 0);
  array_for_t(s->members, DynArray, arr) { *arr = dynarray_create_t(alloc, EcsEntityId, 0); }
  return s;
}

static void set_storage_destroy(SetStorage* s) {
  array_for_t(s->members, DynArray, arr) { dynarray_destroy(arr); }
  alloc_free_t(g_allocHeap, s);
}

static u32 set_storage_index(const SetStorage* s, const StringHash set) {
#ifdef VOLO_SIMD
  ASSERT((scene_set_max % 8) == 0, "Only multiple of 8 set counts are supported");

  const SimdVec setVec = simd_vec_broadcast_u32(set);
  for (u32 setIdx = 0; setIdx != scene_set_max; setIdx += 8) {
    const SimdVec eqA    = simd_vec_eq_u32(simd_vec_load(s->ids + setIdx), setVec);
    const SimdVec eqB    = simd_vec_eq_u32(simd_vec_load(s->ids + setIdx + 4), setVec);
    const u32     eqMask = simd_vec_mask_u8(simd_vec_pack_u32_to_u16(eqA, eqB));

    if (eqMask) {
      return setIdx + intrinsic_ctz_32(eqMask) / 2; // Div 2 due to 16 bit entries.
    }
  }
  return sentinel_u32;
#else
  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (s->ids[setIdx] == set) {
      return setIdx;
    }
  }
  return sentinel_u32;
#endif
}

static u32 set_storage_index_free(const SetStorage* s) {
#ifdef VOLO_SIMD
  ASSERT((scene_set_max % 8) == 0, "Only multiple of 8 set counts are supported");

  for (u32 setIdx = 0; setIdx != scene_set_max; setIdx += 8) {
    const SimdVec freeA    = simd_vec_eq_u32(simd_vec_load(s->ids + setIdx), simd_vec_zero());
    const SimdVec freeB    = simd_vec_eq_u32(simd_vec_load(s->ids + setIdx + 4), simd_vec_zero());
    const u32     freeMask = simd_vec_mask_u8(simd_vec_pack_u32_to_u16(freeA, freeB));

    if (freeMask) {
      return setIdx + intrinsic_ctz_32(freeMask) / 2; // Div 2 due to 16 bit entries.
    }
  }
  return sentinel_u32;
#else
  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (!s->ids[setIdx]) {
      return setIdx;
    }
  }
  return sentinel_u32;
#endif
}

static void set_storage_clear(SetStorage* s, const StringHash set) {
  const u32 setIdx = set_storage_index(s, set);
  if (!sentinel_check(setIdx)) {
    s->ids[setIdx] = 0;
    dynarray_clear(&s->members[setIdx]);
  }
}

static bool set_storage_add(SetStorage* s, const StringHash set, const EcsEntityId e) {
  // Attempt to add it to an existing set.
  const u32 setIdx = set_storage_index(s, set);
  if (!sentinel_check(setIdx)) {
    DynArray* members = &s->members[setIdx];
    *(EcsEntityId*)dynarray_find_or_insert_sorted(members, ecs_compare_entity, &e) = e;
    return true;
  }
  // Attempt to add a new set.
  const u32 freeIdx = set_storage_index_free(s);
  if (!sentinel_check(freeIdx)) {
    s->ids[freeIdx]                                     = set;
    s->mainMembers[freeIdx]                             = e;
    *dynarray_push_t(&s->members[freeIdx], EcsEntityId) = e;
    return true;
  }
  // No more space for this set.
  return false;
}

static void set_storage_remove(SetStorage* s, const StringHash set, const EcsEntityId e) {
  const u32 setIdx = set_storage_index(s, set);
  if (!sentinel_check(setIdx)) {
    DynArray*          members = &s->members[setIdx];
    const EcsEntityId* itr     = dynarray_search_binary(members, ecs_compare_entity, &e);
    if (itr) {
      const usize index = itr - dynarray_begin_t(members, EcsEntityId);
      dynarray_remove(members, index, 1);

      if (!members->size) {
        s->ids[setIdx] = 0; // Set is now empty; we can free the slot.
      } else if (e == s->mainMembers[setIdx]) {
        s->mainMembers[setIdx] = *dynarray_begin_t(members, EcsEntityId);
      }
    }
  }
}

typedef bool (*SetStoragePred)(EcsWorld*, EcsEntityId);

static void set_storage_prune(SetStorage* s, EcsWorld* world, const SetStoragePred pred) {
  for (u32 setIdx = 0; setIdx != scene_set_max; ++setIdx) {
    if (!s->ids[setIdx]) {
      continue; // Unused slot.
    }
    DynArray* members    = &s->members[setIdx];
    bool      removedAny = false;
    for (usize i = members->size; i-- > 0;) {
      const EcsEntityId e = dynarray_begin_t(members, EcsEntityId)[i];
      if (!pred(world, e)) {
        dynarray_remove(members, i, 1);
        removedAny = true;
      }
    }
    if (removedAny) {
      if (!members->size) {
        s->ids[setIdx] = 0; // Set is now empty; we can free the slot.
      } else if (!dynarray_search_binary(members, ecs_compare_entity, &s->mainMembers[setIdx])) {
        // Main-member is no longer in the set; assign a new main-member.
        s->mainMembers[setIdx] = *dynarray_begin_t(&s->members[setIdx], EcsEntityId);
      }
    }
  }
}

static bool set_storage_contains(const SetStorage* s, const StringHash set, const EcsEntityId e) {
  const u32 setIdx = set_storage_index(s, set);
  if (!sentinel_check(setIdx)) {
    return dynarray_search_binary((DynArray*)&s->members[setIdx], ecs_compare_entity, &e) != null;
  }
  return false;
}

static u32 set_storage_count(const SetStorage* s, const StringHash set) {
  const u32 setIdx = set_storage_index(s, set);
  if (!sentinel_check(setIdx)) {
    return (u32)s->members[setIdx].size;
  }
  return 0;
}

static EcsEntityId set_storage_main(const SetStorage* s, const StringHash set) {
  const u32 setIdx = set_storage_index(s, set);
  if (!sentinel_check(setIdx)) {
    return s->mainMembers[setIdx];
  }
  return 0;
}

static const EcsEntityId* set_storage_begin(const SetStorage* s, const StringHash set) {
  const u32 setIdx = set_storage_index(s, set);
  if (!sentinel_check(setIdx)) {
    return dynarray_begin_t(&s->members[setIdx], EcsEntityId);
  }
  return null;
}

static const EcsEntityId* set_storage_end(const SetStorage* s, const StringHash set) {
  const u32 setIdx = set_storage_index(s, set);
  if (!sentinel_check(setIdx)) {
    return dynarray_end_t(&s->members[setIdx], EcsEntityId);
  }
  return null;
}

StringHash g_sceneSetUnit;
StringHash g_sceneSetSelected;

static struct {
  String      setName;
  StringHash* setPtr;
  SceneTags   tags;
} g_setWellknownTagEntries[] = {
    {string_static("unit"), &g_sceneSetUnit, SceneTags_Unit},
    {string_static("selected"), &g_sceneSetSelected, SceneTags_Selected},
};

static void set_wellknown_tags_init_locked(void) {
  for (u32 i = 0; i != array_elems(g_setWellknownTagEntries); ++i) {
    const String name = g_setWellknownTagEntries[i].setName;
#if scene_set_wellknown_names
    *g_setWellknownTagEntries[i].setPtr = stringtable_add(g_stringtable, name);
#else
    *g_setWellknownTagEntries[i].setPtr = string_hash(name);
#endif
  }
}

static void set_wellknown_tags_init(void) {
  static bool           g_init;
  static ThreadSpinLock g_initLock;
  if (UNLIKELY(!g_init)) {
    thread_spinlock_lock(&g_initLock);
    if (!g_init) {
      set_wellknown_tags_init_locked();
      g_init = true;
    }
    thread_spinlock_unlock(&g_initLock);
  }
}

static SceneTags set_wellknown_tags(const StringHash set) {
  for (u32 i = 0; i != array_elems(g_setWellknownTagEntries); ++i) {
    if (*g_setWellknownTagEntries[i].setPtr == set) {
      return g_setWellknownTagEntries[i].tags;
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

typedef struct {
  EcsEntityId entity;
  StringHash  set;
} SetSpeculativeAdd;

ecs_comp_define(SceneSetEnvComp) {
  SetStorage* storage;
  DynArray    requests;        // SetRequest[]
  DynArray    speculativeAdds; // SetSpeculativeAdd[]
};

ecs_comp_define(SceneSetMemberComp) { ALIGNAS(16) StringHash sets[scene_set_member_max_sets]; };
ecs_comp_define(SceneSetMemberStateComp) { bool initialized; };

static void ecs_destruct_set_env_comp(void* data) {
  SceneSetEnvComp* env = data;
  set_storage_destroy(env->storage);
  dynarray_destroy(&env->requests);
  dynarray_destroy(&env->speculativeAdds);
}

static bool set_member_contains(const SceneSetMemberComp* member, const StringHash set) {
#ifdef VOLO_SIMD
  ASSERT(scene_set_member_max_sets == 8, "set_member_contains only supports 8 elems at the moment")

  const SimdVec setVec = simd_vec_broadcast_u32(set);
  const SimdVec eqA    = simd_vec_eq_u32(simd_vec_load(member->sets), setVec);
  const SimdVec eqB    = simd_vec_eq_u32(simd_vec_load(member->sets + 4), setVec);
  const u32     eqMask = simd_vec_mask_u8(simd_vec_pack_u32_to_u16(eqA, eqB));

  return eqMask != 0;
#else
  for (u32 i = 0; i != array_elems(member->sets); ++i) {
    if (member->sets[i] == set) {
      return true; // Already was part of this set.
    }
  }
  return false;
#endif
}

static bool set_member_add(SceneSetMemberComp* member, const StringHash set) {
#ifdef VOLO_SIMD
  ASSERT(scene_set_member_max_sets == 8, "set_member_add only supports 8 elems at the moment")

  const SimdVec setVec      = simd_vec_broadcast_u32(set);
  const SimdVec memberSetsA = simd_vec_load(member->sets);
  const SimdVec memberSetsB = simd_vec_load(member->sets + 4);

  const SimdVec eqA    = simd_vec_eq_u32(memberSetsA, setVec);
  const SimdVec eqB    = simd_vec_eq_u32(memberSetsB, setVec);
  const u32     eqMask = simd_vec_mask_u8(simd_vec_pack_u32_to_u16(eqA, eqB));

  if (eqMask) {
    return true; // Member already has the given set.
  }

  const SimdVec freeA    = simd_vec_eq_u32(memberSetsA, simd_vec_zero());
  const SimdVec freeB    = simd_vec_eq_u32(memberSetsB, simd_vec_zero());
  const u32     freeMask = simd_vec_mask_u8(simd_vec_pack_u32_to_u16(freeA, freeB));

  if (freeMask) {
    const u32 freeIdx     = intrinsic_ctz_32(freeMask) / 2; // Div 2 due to 16 bit entries.
    member->sets[freeIdx] = set;
    return true; // Successfully added.
  }

  return false;
#else
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
#endif
}

static bool set_member_remove(SceneSetMemberComp* member, const StringHash set) {
#ifdef VOLO_SIMD
  ASSERT(scene_set_member_max_sets == 8, "set_member_contains only supports 8 elems at the moment")

  const SimdVec setVec = simd_vec_broadcast_u32(set);
  const SimdVec eqA    = simd_vec_eq_u32(simd_vec_load(member->sets), setVec);
  const SimdVec eqB    = simd_vec_eq_u32(simd_vec_load(member->sets + 4), setVec);
  const u32     eqMask = simd_vec_mask_u8(simd_vec_pack_u32_to_u16(eqA, eqB));

  if (eqMask) {
    const u32 eqIdx     = intrinsic_ctz_32(eqMask) / 2; // Div 2 due to 16 bit entries.
    member->sets[eqIdx] = 0;
    return true;
  }

  return false;
#else
  for (u32 i = 0; i != array_elems(member->sets); ++i) {
    if (member->sets[i] == set) {
      member->sets[i] = 0;
      return true;
    }
  }
  return false;
#endif
}

static void ecs_combine_set_member(void* dataA, void* dataB) {
  SceneSetMemberComp* compA = dataA;
  SceneSetMemberComp* compB = dataB;

  for (u32 i = 0; i != array_elems(compB->sets); ++i) {
    if (compB->sets[i]) {
      if (!UNLIKELY(set_member_add(compA, compB->sets[i]))) {
        log_e(
            "Set member limit reached during combine",
            log_param("limit", fmt_int(array_elems(compB->sets))));
      }
    }
  }
}

static void ecs_combine_set_member_state(void* dataA, void* dataB) {
  ((SceneSetMemberStateComp*)dataA)->initialized = false;
  (void)dataB;
}

ecs_view_define(EnvView) { ecs_access_write(SceneSetEnvComp); }

ecs_view_define(MemberView) {
  ecs_access_write(SceneSetMemberComp);
  ecs_access_write(SceneSetMemberStateComp);
  ecs_access_maybe_write(SceneTagComp);
}

static bool set_member_valid(EcsWorld* world, const EcsEntityId e) {
  return ecs_world_exists(world, e) && ecs_world_has_t(world, e, SceneSetMemberComp);
}

ecs_system_define(SceneSetInitSys) {
  const EcsEntityId global = ecs_world_global(world);
  EcsIterator*      envItr = ecs_view_maybe_at(ecs_world_view_t(world, EnvView), global);
  if (!envItr) {
    ecs_world_add_t(
        world,
        global,
        SceneSetEnvComp,
        .storage         = set_storage_create(g_allocHeap),
        .requests        = dynarray_create_t(g_allocHeap, SetRequest, 128),
        .speculativeAdds = dynarray_create_t(g_allocHeap, SetSpeculativeAdd, 128));
    return;
  }

  SceneSetEnvComp* env = ecs_view_write_t(envItr, SceneSetEnvComp);

  // Prune the removed entities from all sets.
  set_storage_prune(env->storage, world, set_member_valid);

  // TODO: When removing the SceneSetMemberComp component from an entity that was in a well-known
  // set with tags then currently those tags are not cleared.

  EcsView* memberView = ecs_world_view_t(world, MemberView);
  for (EcsIterator* itr = ecs_view_itr(memberView); ecs_view_walk(itr);) {
    const EcsEntityId        entity      = ecs_view_entity(itr);
    SceneSetMemberStateComp* memberState = ecs_view_write_t(itr, SceneSetMemberStateComp);
    if (memberState->initialized) {
      continue;
    }
    SceneSetMemberComp* member  = ecs_view_write_t(itr, SceneSetMemberComp);
    SceneTagComp*       tagComp = ecs_view_write_t(itr, SceneTagComp);

    for (u32 i = 0; i != array_elems(member->sets); ++i) {
      if (!member->sets[i]) {
        continue; // Unused slot.
      }
      if (UNLIKELY(!set_storage_add(env->storage, member->sets[i], entity))) {
        log_e("Set limit reached during init", log_param("limit", fmt_int(scene_set_max)));
        set_member_remove(member, member->sets[i]);
        break;
      }
      if (tagComp) {
        tagComp->tags |= set_wellknown_tags(member->sets[i]);
      }
    }
    memberState->initialized = true;
  }
}

ecs_system_define(SceneSetUpdateSys) {
  const EcsEntityId global = ecs_world_global(world);
  EcsIterator*      envItr = ecs_view_maybe_at(ecs_world_view_t(world, EnvView), global);
  if (!envItr) {
    return;
  }
  SceneSetEnvComp* env = ecs_view_write_t(envItr, SceneSetEnvComp);

  EcsView*     memberView = ecs_world_view_t(world, MemberView);
  EcsIterator* itr        = ecs_view_itr(memberView);

  // Verify consistency of speculative adds.
  dynarray_for_t(&env->speculativeAdds, SetSpeculativeAdd, add) {
    if (ecs_view_maybe_jump(itr, add->entity)) {
      if (UNLIKELY(!set_member_contains(ecs_view_read_t(itr, SceneSetMemberComp), add->set))) {
        set_storage_remove(env->storage, add->set, add->entity);
      }
    }
  }
  dynarray_clear(&env->speculativeAdds);

  // Handle requests.
  dynarray_for_t(&env->requests, SetRequest, req) {
    switch (req->type) {
    case SetRequestType_Add: {
      if (!ecs_world_exists(world, req->target)) {
        continue;
      }
      bool                success = true;
      SceneSetMemberComp* member  = null;
      if (ecs_view_maybe_jump(itr, req->target)) {
        member = ecs_view_write_t(itr, SceneSetMemberComp);
        if (LIKELY(set_member_add(member, req->set))) {
          SceneTagComp* tagComp = ecs_view_write_t(itr, SceneTagComp);
          if (tagComp) {
            tagComp->tags |= set_wellknown_tags(req->set);
          }
        } else {
          log_e("Set member limit reached", log_param("limit", fmt_int(array_elems(member->sets))));
          success = false;
        }
      } else {
        ecs_world_add_t(world, req->target, SceneSetMemberComp, .sets[0] = req->set);
        ecs_world_add_t(world, req->target, SceneSetMemberStateComp);

        /**
         * Because we have a per-member limit, the member-add might fail (during component combine)
         * and in that case we end up in an inconsistent state (where its in the storage but not the
         * member). To avoid this we mark these speculative-adds and remove them from the storage in
         * the next tick if the member-add failed.
         */
        *dynarray_push_t(&env->speculativeAdds, SetSpeculativeAdd) = (SetSpeculativeAdd){
            .entity = req->target,
            .set    = req->set,
        };
      }
      if (LIKELY(success) && UNLIKELY(!set_storage_add(env->storage, req->set, req->target))) {
        log_e("Set limit reached", log_param("limit", fmt_int(scene_set_max)));
        if (member) {
          set_member_remove(member, req->set); // Fixup the member to stay consistent.
        }
      }
      continue;
    }
    case SetRequestType_Remove: {
      if (ecs_view_maybe_jump(itr, req->target)) {
        SceneSetMemberComp* member = ecs_view_write_t(itr, SceneSetMemberComp);
        if (set_member_remove(member, req->set)) {
          SceneTagComp* tagComp = ecs_view_write_t(itr, SceneTagComp);
          if (tagComp) {
            tagComp->tags &= ~set_wellknown_tags(req->set);
          }
        }
      }
      set_storage_remove(env->storage, req->set, req->target);
      continue;
    }
    case SetRequestType_Clear: {
      for (ecs_view_itr_reset(itr); ecs_view_walk(itr);) {
        SceneSetMemberComp* member = ecs_view_write_t(itr, SceneSetMemberComp);
        if (set_member_remove(member, req->set)) {
          SceneTagComp* tagComp = ecs_view_write_t(itr, SceneTagComp);
          if (tagComp) {
            tagComp->tags &= ~set_wellknown_tags(req->set);
          }
        }
      }
      set_storage_clear(env->storage, req->set);
      continue;
    }
    }
    diag_crash_msg("Unsupported selection request type");
  }
  dynarray_clear(&env->requests);
}

ecs_module_init(scene_set_module) {
  set_wellknown_tags_init();

  ecs_register_comp(SceneSetEnvComp, .destructor = ecs_destruct_set_env_comp);
  ecs_register_comp(SceneSetMemberComp, .combinator = ecs_combine_set_member);
  ecs_register_comp(SceneSetMemberStateComp, .combinator = ecs_combine_set_member_state);

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
  ecs_world_add_t(world, e, SceneSetMemberStateComp);
}

bool scene_set_member_contains(const SceneSetMemberComp* member, const StringHash set) {
  return set_member_contains(member, set);
}

u32 scene_set_member_all(
    const SceneSetMemberComp* member, StringHash out[PARAM_ARRAY_SIZE(scene_set_member_max_sets)]) {
  u32 count = 0;
  for (u32 i = 0; i != array_elems(member->sets); ++i) {
    if (member->sets[i]) {
      out[count++] = member->sets[i];
    }
  }
  return count;
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
