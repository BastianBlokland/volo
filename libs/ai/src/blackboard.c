#include "ai_blackboard.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"

#define blackboard_slots_initial 32
#define blackboard_slots_loadfactor 0.75f

typedef struct {
  StringHash key;
  AiValue    value; // TODO: The alignment hurts us, investigate inlining part of the value.
} AiBlackboardSlot;

struct sAiBlackboard {
  Allocator*        alloc;
  u32               slotCount, slotCountUsed;
  AiBlackboardSlot* slots;
};

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
    if (slot->key) {
      *blackboard_slot(newSlots, newSlotCount, slot->key) = *slot;
    }
  }

  // Free the old slots.
  alloc_free_array_t(bb->alloc, bb->slots, bb->slotCount);
  bb->slots     = newSlots;
  bb->slotCount = newSlotCount;
}

static AiBlackboardSlot* ai_blackboard_insert(AiBlackboard* bb, const StringHash key) {
  AiBlackboardSlot* slot = blackboard_slot(bb->slots, bb->slotCount, key);
  if (!slot->key) {
    slot->key = key;
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

AiValue ai_blackboard_get(const AiBlackboard* bb, const StringHash key) {
  return blackboard_slot(bb->slots, bb->slotCount, key)->value;
}

void ai_blackboard_set(AiBlackboard* bb, const StringHash key, const AiValue value) {
  ai_blackboard_insert(bb, key)->value = value;
}

void ai_blackboard_set_none(AiBlackboard* bb, const StringHash key) {
  ai_blackboard_insert(bb, key)->value = ai_value_none();
}

AiBlackboardItr ai_blackboard_begin(const AiBlackboard* bb) {
  return ai_blackboard_next(bb, (AiBlackboardItr){0});
}

AiBlackboardItr ai_blackboard_next(const AiBlackboard* bb, const AiBlackboardItr itr) {
  for (u32 i = itr.next; i < bb->slotCount; ++i) {
    const AiBlackboardSlot* slot = &bb->slots[i];
    if (slot->key) {
      return (AiBlackboardItr){.key = slot->key, .next = i + 1};
    }
  }
  return (AiBlackboardItr){.key = 0, .next = sentinel_u32};
}
