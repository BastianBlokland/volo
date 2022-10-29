#include "ai_blackboard.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"

#define blackboard_slots_initial 32
#define blackboard_slots_loadfactor 0.75f

struct sAiBlackboard {
  Allocator*  alloc;
  u32         slotCount, slotCountUsed;
  StringHash* slotKeys;
  AiValue*    slotValues;
};

static u32 blackboard_should_grow(AiBlackboard* bb) {
  return bb->slotCountUsed >= (u32)(bb->slotCount * blackboard_slots_loadfactor);
}

static StringHash* blackboard_slot_keys_alloc(Allocator* alloc, const u32 slotCount) {
  StringHash* keys = alloc_array_t(alloc, StringHash, slotCount);
  mem_set(mem_create(keys, sizeof(StringHash) * slotCount), 0); // Zero init all keys.
  return keys;
}

static AiValue* blackboard_slot_values_alloc(Allocator* alloc, const u32 slotCount) {
  AiValue* values = alloc_array_t(alloc, AiValue, slotCount);
  mem_set(mem_create(values, sizeof(AiValue) * slotCount), 0); // Zero init all values.
  return values;
}

static u32 blackboard_slot_index(StringHash* slotKeys, const u32 slotCount, const StringHash key) {
  diag_assert_msg(key, "Empty blackboard key is not valid");

  u32 slotIndex = key & (slotCount - 1);
  for (u32 i = 0; i != slotCount; ++i) {
    if (LIKELY(!slotKeys[slotIndex] || slotKeys[slotIndex] == key)) {
      return slotIndex; // Slot is either empty or matches the desired key.
    }
    // Key collision, jump to a new place in the blackboard (quadratic probing).
    slotIndex = (slotIndex + i + 1) & (slotCount - 1);
  }
  diag_crash_msg("No available blackboard slots");
}

static void blackboard_grow(AiBlackboard* bb) {
  // Allocate new slots.
  const u32   newSlotCount  = bits_nextpow2_32(bb->slotCount + 1);
  StringHash* newSlotKeys   = blackboard_slot_keys_alloc(bb->alloc, newSlotCount);
  AiValue*    newSlotValues = blackboard_slot_values_alloc(bb->alloc, newSlotCount);

  // Insert the existing data into the new slots.
  for (u32 oldSlotIndex = 0; oldSlotIndex != bb->slotCount; ++oldSlotIndex) {
    const StringHash key = bb->slotKeys[oldSlotIndex];
    if (key) {
      const u32 newSlotIndex      = blackboard_slot_index(newSlotKeys, newSlotCount, key);
      newSlotKeys[newSlotIndex]   = key;
      newSlotValues[newSlotIndex] = bb->slotValues[oldSlotIndex];
    }
  }

  // Free the old slots.
  alloc_free_array_t(bb->alloc, bb->slotKeys, bb->slotCount);
  alloc_free_array_t(bb->alloc, bb->slotValues, bb->slotCount);
  bb->slotKeys   = newSlotKeys;
  bb->slotValues = newSlotValues;
  bb->slotCount  = newSlotCount;
}

static u32 ai_blackboard_insert(AiBlackboard* bb, const StringHash key) {
  u32 slotIndex = blackboard_slot_index(bb->slotKeys, bb->slotCount, key);
  if (!bb->slotKeys[slotIndex]) {
    // New entry; initialize and test if we need to grow the table.
    bb->slotKeys[slotIndex] = key;
    ++bb->slotCountUsed;
    if (UNLIKELY(blackboard_should_grow(bb))) {
      blackboard_grow(bb);
      // Re-query the slot after growing the table as the previous pointer is no longer valid.
      slotIndex = blackboard_slot_index(bb->slotKeys, bb->slotCount, key);
    }
  }
  return slotIndex;
}

AiBlackboard* ai_blackboard_create(Allocator* alloc) {
  AiBlackboard* bb = alloc_alloc_t(alloc, AiBlackboard);

  diag_assert(bits_ispow2_32(blackboard_slots_initial));

  *bb = (AiBlackboard){
      .alloc      = alloc,
      .slotCount  = blackboard_slots_initial,
      .slotKeys   = blackboard_slot_keys_alloc(alloc, blackboard_slots_initial),
      .slotValues = blackboard_slot_values_alloc(alloc, blackboard_slots_initial),
  };
  return bb;
}

void ai_blackboard_destroy(AiBlackboard* bb) {
  alloc_free_array_t(bb->alloc, bb->slotKeys, bb->slotCount);
  alloc_free_array_t(bb->alloc, bb->slotValues, bb->slotCount);
  alloc_free_t(bb->alloc, bb);
}

AiValue ai_blackboard_get(const AiBlackboard* bb, const StringHash key) {
  const u32 slotIndex = blackboard_slot_index(bb->slotKeys, bb->slotCount, key);
  return bb->slotValues[slotIndex];
}

void ai_blackboard_set(AiBlackboard* bb, const StringHash key, const AiValue value) {
  const u32 slotIndex       = ai_blackboard_insert(bb, key);
  bb->slotValues[slotIndex] = value;
}

void ai_blackboard_set_none(AiBlackboard* bb, const StringHash key) {
  const u32 slotIndex       = ai_blackboard_insert(bb, key);
  bb->slotValues[slotIndex] = ai_value_none();
}

AiBlackboardItr ai_blackboard_begin(const AiBlackboard* bb) {
  return ai_blackboard_next(bb, (AiBlackboardItr){0});
}

AiBlackboardItr ai_blackboard_next(const AiBlackboard* bb, const AiBlackboardItr itr) {
  for (u32 i = itr.next; i < bb->slotCount; ++i) {
    if (bb->slotKeys[i]) {
      return (AiBlackboardItr){.key = bb->slotKeys[i], .next = i + 1};
    }
  }
  return (AiBlackboardItr){.key = 0, .next = sentinel_u32};
}
