#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "script_mem.h"

#include "val_internal.h"

#define script_mem_slots_initial 32
#define script_mem_slots_loadfactor 0.75f

static u32 script_mem_should_grow(ScriptMem* mem) {
  return mem->slotCountUsed >= (u32)(mem->slotCount * script_mem_slots_loadfactor);
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

static void script_mem_grow(ScriptMem* mem) {
  // Allocate new slots.
  const u32   newSlotCount  = bits_nextpow2_32(mem->slotCount + 1);
  StringHash* newSlotKeys   = script_mem_slot_keys_alloc(g_allocHeap, newSlotCount);
  ScriptVal*  newSlotValues = script_mem_slot_values_alloc(g_allocHeap, newSlotCount);

  // Insert the existing data into the new slots.
  for (u32 oldSlotIndex = 0; oldSlotIndex != mem->slotCount; ++oldSlotIndex) {
    const StringHash key = mem->slotKeys[oldSlotIndex];
    if (key) {
      const u32 newSlotIndex      = script_mem_slot_index(newSlotKeys, newSlotCount, key);
      newSlotKeys[newSlotIndex]   = key;
      newSlotValues[newSlotIndex] = mem->slotValues[oldSlotIndex];
    }
  }

  // Free the old slots.
  alloc_free_array_t(g_allocHeap, mem->slotKeys, mem->slotCount);
  alloc_free_array_t(g_allocHeap, mem->slotValues, mem->slotCount);
  mem->slotKeys   = newSlotKeys;
  mem->slotValues = newSlotValues;
  mem->slotCount  = newSlotCount;
}

static u32 script_mem_insert(ScriptMem* mem, const StringHash key) {
  u32 slotIndex = script_mem_slot_index(mem->slotKeys, mem->slotCount, key);
  if (!mem->slotKeys[slotIndex]) {
    // New entry; initialize and test if we need to grow the table.
    mem->slotKeys[slotIndex] = key;
    ++mem->slotCountUsed;
    if (UNLIKELY(script_mem_should_grow(mem))) {
      script_mem_grow(mem);
      // Re-query the slot after growing the table as the previous pointer is no longer valid.
      slotIndex = script_mem_slot_index(mem->slotKeys, mem->slotCount, key);
    }
  }
  return slotIndex;
}

ScriptMem script_mem_create(void) {
  diag_assert(bits_ispow2_32(script_mem_slots_initial));

  return (ScriptMem){
      .slotCount  = script_mem_slots_initial,
      .slotKeys   = script_mem_slot_keys_alloc(g_allocHeap, script_mem_slots_initial),
      .slotValues = script_mem_slot_values_alloc(g_allocHeap, script_mem_slots_initial),
  };
}

void script_mem_destroy(ScriptMem* mem) {
  alloc_free_array_t(g_allocHeap, mem->slotKeys, mem->slotCount);
  alloc_free_array_t(g_allocHeap, mem->slotValues, mem->slotCount);
}

ScriptVal script_mem_load(const ScriptMem* mem, const StringHash key) {
  const u32 slotIndex = script_mem_slot_index(mem->slotKeys, mem->slotCount, key);
  return mem->slotValues[slotIndex];
}

void script_mem_store(ScriptMem* mem, const StringHash key, const ScriptVal value) {
  const u32 slotIndex        = script_mem_insert(mem, key);
  mem->slotValues[slotIndex] = value;
}

ScriptMemItr script_mem_begin(const ScriptMem* mem) {
  return script_mem_next(mem, (ScriptMemItr){0});
}

ScriptMemItr script_mem_next(const ScriptMem* mem, const ScriptMemItr itr) {
  for (u32 i = itr.next; i < mem->slotCount; ++i) {
    if (mem->slotKeys[i]) {
      return (ScriptMemItr){.key = mem->slotKeys[i], .next = i + 1};
    }
  }
  return (ScriptMemItr){.key = 0, .next = sentinel_u32};
}
