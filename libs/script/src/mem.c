#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "script_mem.h"

#define script_mem_slots_initial 32
#define script_mem_slots_loadfactor 0.75f

struct sScriptMem {
  Allocator*  alloc;
  u32         slotCount, slotCountUsed;
  StringHash* slotKeys;
  ScriptVal*  slotValues;
};

static u32 script_mem_should_grow(ScriptMem* bb) {
  return bb->slotCountUsed >= (u32)(bb->slotCount * script_mem_slots_loadfactor);
}

static StringHash* script_mem_slot_keys_alloc(Allocator* alloc, const u32 slotCount) {
  StringHash* keys = alloc_array_t(alloc, StringHash, slotCount);
  mem_set(mem_create(keys, sizeof(StringHash) * slotCount), 0); // Zero init all keys.
  return keys;
}

static ScriptVal* script_mem_slot_values_alloc(Allocator* alloc, const u32 slotCount) {
  ScriptVal* values = alloc_array_t(alloc, ScriptVal, slotCount);
  mem_set(mem_create(values, sizeof(ScriptVal) * slotCount), 0); // Zero init all values.
  return values;
}

static u32 script_mem_slot_index(StringHash* slotKeys, const u32 slotCount, const StringHash key) {
  diag_assert_msg(key, "Empty memory key is not valid");

  u32 slotIndex = key & (slotCount - 1);
  for (u32 i = 0; i != slotCount; ++i) {
    if (LIKELY(!slotKeys[slotIndex] || slotKeys[slotIndex] == key)) {
      return slotIndex; // Slot is either empty or matches the desired key.
    }
    // Key collision, jump to a new place in the memory (quadratic probing).
    slotIndex = (slotIndex + i + 1) & (slotCount - 1);
  }
  diag_crash_msg("No available memory slots");
}

static void script_mem_grow(ScriptMem* bb) {
  // Allocate new slots.
  const u32   newSlotCount  = bits_nextpow2_32(bb->slotCount + 1);
  StringHash* newSlotKeys   = script_mem_slot_keys_alloc(bb->alloc, newSlotCount);
  ScriptVal*  newSlotValues = script_mem_slot_values_alloc(bb->alloc, newSlotCount);

  // Insert the existing data into the new slots.
  for (u32 oldSlotIndex = 0; oldSlotIndex != bb->slotCount; ++oldSlotIndex) {
    const StringHash key = bb->slotKeys[oldSlotIndex];
    if (key) {
      const u32 newSlotIndex      = script_mem_slot_index(newSlotKeys, newSlotCount, key);
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

static u32 script_mem_insert(ScriptMem* bb, const StringHash key) {
  u32 slotIndex = script_mem_slot_index(bb->slotKeys, bb->slotCount, key);
  if (!bb->slotKeys[slotIndex]) {
    // New entry; initialize and test if we need to grow the table.
    bb->slotKeys[slotIndex] = key;
    ++bb->slotCountUsed;
    if (UNLIKELY(script_mem_should_grow(bb))) {
      script_mem_grow(bb);
      // Re-query the slot after growing the table as the previous pointer is no longer valid.
      slotIndex = script_mem_slot_index(bb->slotKeys, bb->slotCount, key);
    }
  }
  return slotIndex;
}

ScriptMem* script_mem_create(Allocator* alloc) {
  ScriptMem* bb = alloc_alloc_t(alloc, ScriptMem);

  diag_assert(bits_ispow2_32(script_mem_slots_initial));

  *bb = (ScriptMem){
      .alloc      = alloc,
      .slotCount  = script_mem_slots_initial,
      .slotKeys   = script_mem_slot_keys_alloc(alloc, script_mem_slots_initial),
      .slotValues = script_mem_slot_values_alloc(alloc, script_mem_slots_initial),
  };
  return bb;
}

void script_mem_destroy(ScriptMem* bb) {
  alloc_free_array_t(bb->alloc, bb->slotKeys, bb->slotCount);
  alloc_free_array_t(bb->alloc, bb->slotValues, bb->slotCount);
  alloc_free_t(bb->alloc, bb);
}

ScriptVal script_mem_get(const ScriptMem* bb, const StringHash key) {
  const u32 slotIndex = script_mem_slot_index(bb->slotKeys, bb->slotCount, key);
  return bb->slotValues[slotIndex];
}

void script_mem_set(ScriptMem* bb, const StringHash key, const ScriptVal value) {
  const u32 slotIndex       = script_mem_insert(bb, key);
  bb->slotValues[slotIndex] = value;
}

void script_mem_set_null(ScriptMem* bb, const StringHash key) {
  const u32 slotIndex       = script_mem_insert(bb, key);
  bb->slotValues[slotIndex] = script_null();
}

ScriptMemItr script_mem_begin(const ScriptMem* bb) {
  return script_mem_next(bb, (ScriptMemItr){0});
}

ScriptMemItr script_mem_next(const ScriptMem* bb, const ScriptMemItr itr) {
  for (u32 i = itr.next; i < bb->slotCount; ++i) {
    if (bb->slotKeys[i]) {
      return (ScriptMemItr){.key = bb->slotKeys[i], .next = i + 1};
    }
  }
  return (ScriptMemItr){.key = 0, .next = sentinel_u32};
}
