#include "ai_blackboard.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"

#define blackboard_slots_initial 32
#define blackboard_slots_loadfactor 0.75f

typedef enum {
  AiBlackboard_Active = 1 << 0,
} AiBlackboardFlags;

typedef struct {
  StringHash        key;
  AiBlackboardType  type;
  AiBlackboardFlags flags;
  union {
    f64          _f64;
    bool         _bool;
    GeoVector    vector;
    TimeDuration time;
    EcsEntityId  entity;
  } data;
} AiBlackboardSlot;

struct sAiBlackboard {
  Allocator*        alloc;
  u32               slotCount, slotCountUsed;
  AiBlackboardSlot* slots;
};

MAYBE_UNUSED void
blackboard_assert_type(const AiBlackboardSlot* slot, const AiBlackboardType type) {
  diag_assert_msg(
      slot->type == type,
      "Mismatching knowledge type, expected: '{}', actual: '{}'.",
      fmt_text(ai_blackboard_type_str(type)),
      fmt_text(ai_blackboard_type_str(slot->type)));
}

static u32 blackboard_should_grow(AiBlackboard* bb) {
  return bb->slotCountUsed >= (u32)(bb->slotCount * blackboard_slots_loadfactor);
}

static AiBlackboardSlot* blackboard_slots_alloc(Allocator* alloc, const u32 slotCount) {
  const usize slotsMemSize = sizeof(AiBlackboardSlot) * slotCount;
  const Mem   slotsMem     = alloc_alloc(alloc, slotsMemSize, alignof(AiBlackboardSlot));
  mem_set(slotsMem, 0);
  return slotsMem.ptr;
}

static AiBlackboardSlot*
blackboard_slot(AiBlackboardSlot* slots, const u32 slotCount, const StringHash key) {
  diag_assert(key); // Key of 0 is invalid.

  u32 bucket = key & (slotCount - 1);
  for (usize i = 0; i != slotCount; ++i) {
    AiBlackboardSlot* slot = &slots[bucket];
    if (LIKELY(!slot->key || slot->key == key)) {
      return slot; // Slot is either empty or matches the desired key.
    }
    // Key collision, jump to a new place in the blackboard (quadratic probing).
    bucket = (bucket + i + 1) & (slotCount - 1);
  }
  diag_crash_msg("No available blackboard slots");
}

static void blackboard_grow(AiBlackboard* bb) {
  // Allocate new slots.
  const u32         newSlotCount = bits_nextpow2_32(bb->slotCount + 1);
  AiBlackboardSlot* newSlots     = blackboard_slots_alloc(bb->alloc, newSlotCount);

  // Insert the existing data into the new slots.
  for (AiBlackboardSlot* slot = bb->slots; slot != (bb->slots + bb->slotCount); ++slot) {
    if (slot->flags & AiBlackboard_Active) {
      *blackboard_slot(newSlots, newSlotCount, slot->key) = *slot;
    }
  }

  // Free the old slots.
  alloc_free_array_t(bb->alloc, bb->slots, bb->slotCount);
  bb->slots     = newSlots;
  bb->slotCount = newSlotCount;
}

static AiBlackboardSlot*
ai_blackboard_insert(AiBlackboard* bb, const StringHash key, const AiBlackboardType type) {
  AiBlackboardSlot* slot = blackboard_slot(bb->slots, bb->slotCount, key);
  if (slot->key) {
    /**
     * Key already existed in the blackboard.
     */
    diag_assert_msg(
        slot->type == type,
        "Knowledge type conflict while inserting, new: '{}', existing: '{}'.",
        fmt_text(ai_blackboard_type_str(slot->type)),
        fmt_text(ai_blackboard_type_str(type)));
    slot->type = type; // Limp along when running without assertions.
    slot->flags |= AiBlackboard_Active;
  } else {
    /**
     * New entry in the blackboard.
     */
    slot->key  = key;
    slot->type = type;
    slot->flags |= AiBlackboard_Active;
    ++bb->slotCountUsed;
    if (UNLIKELY(blackboard_should_grow(bb))) {
      blackboard_grow(bb);
      // Re-query the slot after growing the table as the previous pointer is no longer valid.
      slot = blackboard_slot(bb->slots, bb->slotCount, key);
    }
  }
  return slot;
}

AiBlackboard* ai_blackboard_create(Allocator* alloc) {
  AiBlackboard* bb = alloc_alloc_t(alloc, AiBlackboard);

  diag_assert(bits_ispow2_32(blackboard_slots_initial));

  *bb = (AiBlackboard){
      .alloc     = alloc,
      .slotCount = blackboard_slots_initial,
      .slots     = blackboard_slots_alloc(alloc, blackboard_slots_initial),
  };
  return bb;
}

void ai_blackboard_destroy(AiBlackboard* bb) {
  alloc_free_array_t(bb->alloc, bb->slots, bb->slotCount);
  alloc_free_t(bb->alloc, bb);
}

String ai_blackboard_type_str(const AiBlackboardType type) {
  static const String g_names[] = {
      string_static("Invalid"),
      string_static("f64"),
      string_static("Bool"),
      string_static("Vector"),
      string_static("Time"),
      string_static("Entity"),
  };
  ASSERT(array_elems(g_names) == AiBlackboardType_Count, "Incorrect number of names");
  return g_names[type];
}

AiBlackboardType ai_blackboard_type(const AiBlackboard* bb, const StringHash key) {
  return blackboard_slot(bb->slots, bb->slotCount, key)->type;
}

bool ai_blackboard_exists(const AiBlackboard* bb, const StringHash key) {
  return (blackboard_slot(bb->slots, bb->slotCount, key)->flags & AiBlackboard_Active) != 0;
}

f64 ai_blackboard_get_f64(const AiBlackboard* bb, const StringHash key) {
  const AiBlackboardSlot* slot = blackboard_slot(bb->slots, bb->slotCount, key);
  if (slot->flags & AiBlackboard_Active) {
    blackboard_assert_type(slot, AiBlackboardType_f64);
    return slot->data._f64;
  }
  return 0; // Default.
}

bool ai_blackboard_get_bool(const AiBlackboard* bb, const StringHash key) {
  const AiBlackboardSlot* slot = blackboard_slot(bb->slots, bb->slotCount, key);
  if (slot->flags & AiBlackboard_Active) {
    blackboard_assert_type(slot, AiBlackboardType_Bool);
    return slot->data._bool;
  }
  return false; // Default.
}

GeoVector ai_blackboard_get_vector(const AiBlackboard* bb, const StringHash key) {
  const AiBlackboardSlot* slot = blackboard_slot(bb->slots, bb->slotCount, key);
  if (slot->flags & AiBlackboard_Active) {
    blackboard_assert_type(slot, AiBlackboardType_Vector);
    return slot->data.vector;
  }
  return geo_vector(0); // Default.
}

TimeDuration ai_blackboard_get_time(const AiBlackboard* bb, const StringHash key) {
  const AiBlackboardSlot* slot = blackboard_slot(bb->slots, bb->slotCount, key);
  if (slot->flags & AiBlackboard_Active) {
    blackboard_assert_type(slot, AiBlackboardType_Time);
    return slot->data.time;
  }
  return (TimeDuration)0; // Default.
}

EcsEntityId ai_blackboard_get_entity(const AiBlackboard* bb, const StringHash key) {
  const AiBlackboardSlot* slot = blackboard_slot(bb->slots, bb->slotCount, key);
  if (slot->flags & AiBlackboard_Active) {
    blackboard_assert_type(slot, AiBlackboardType_Entity);
    return slot->data.entity;
  }
  return (EcsEntityId)0; // Default.
}

void ai_blackboard_set_f64(AiBlackboard* bb, const StringHash key, const f64 value) {
  ai_blackboard_insert(bb, key, AiBlackboardType_f64)->data._f64 = value;
}

void ai_blackboard_set_bool(AiBlackboard* bb, const StringHash key, const bool value) {
  ai_blackboard_insert(bb, key, AiBlackboardType_Bool)->data._bool = value;
}

void ai_blackboard_set_vector(AiBlackboard* bb, const StringHash key, const GeoVector value) {
  ai_blackboard_insert(bb, key, AiBlackboardType_Vector)->data.vector = value;
}

void ai_blackboard_set_time(AiBlackboard* bb, const StringHash key, const TimeDuration value) {
  ai_blackboard_insert(bb, key, AiBlackboardType_Time)->data.time = value;
}

void ai_blackboard_set_entity(AiBlackboard* bb, const StringHash key, const EcsEntityId value) {
  ai_blackboard_insert(bb, key, AiBlackboardType_Entity)->data.entity = value;
}

void ai_blackboard_unset(AiBlackboard* bb, const StringHash key) {
  AiBlackboardSlot* slot = blackboard_slot(bb->slots, bb->slotCount, key);
  slot->flags &= ~AiBlackboard_Active;
  mem_set(mem_var(slot->data), 0);
}

void ai_blackboard_copy(AiBlackboard* bb, const StringHash srcKey, const StringHash dstKey) {
  const AiBlackboardSlot* srcSlot = blackboard_slot(bb->slots, bb->slotCount, srcKey);
  if (srcSlot->flags & AiBlackboard_Active) {
    AiBlackboardSlot* dstSlot = ai_blackboard_insert(bb, dstKey, srcSlot->type);
    mem_cpy(mem_var(dstSlot->data), mem_var(srcSlot->data));
  }
}

AiBlackboardItr ai_blackboard_begin(const AiBlackboard* bb) {
  return ai_blackboard_next(bb, (AiBlackboardItr){0});
}

AiBlackboardItr ai_blackboard_next(const AiBlackboard* bb, const AiBlackboardItr itr) {
  for (u32 i = itr.next; i < bb->slotCount; ++i) {
    const AiBlackboardSlot* slot = &bb->slots[i];
    if (slot->flags & AiBlackboard_Active) {
      return (AiBlackboardItr){.key = slot->key, .next = i + 1};
    }
  }
  return (AiBlackboardItr){.key = 0, .next = sentinel_u32};
}

bool ai_blackboard_equals(const AiBlackboard* bb, const StringHash keyA, const StringHash keyB) {
  const AiBlackboardSlot* slotA = blackboard_slot(bb->slots, bb->slotCount, keyA);
  const AiBlackboardSlot* slotB = blackboard_slot(bb->slots, bb->slotCount, keyB);
  if (!(slotA->flags & slotB->flags & AiBlackboard_Active)) {
    return false; // Either (or both) of the keys are not active.
  }
  if (slotA->type != slotB->type) {
    return false; // Type mismatch.
  }
  return mem_eq(mem_var(slotA->data), mem_var(slotB->data));
}
