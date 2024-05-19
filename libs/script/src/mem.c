#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "script_mem.h"

#include "val_internal.h"

#define script_mem_slots_initial 32
#define script_mem_slots_loadfactor 0.75f

/**
 * Values and keys are stored together in one allocation.
 * First all values (because they have a higher alignment requirement) and then all values.
 */

ASSERT(alignof(ScriptVal) > alignof(StringHash), "Padding should not be required between types");

#define slot_data_values(_DATA_) ((ScriptVal*)(_DATA_))

#define slot_data_keys(_DATA_, _SLOT_COUNT_)                                                       \
  ((StringHash*)bits_ptr_offset((_DATA_), sizeof(ScriptVal) * (_SLOT_COUNT_)))

static u32 slot_data_should_grow(ScriptMem* mem) {
  return mem->slotCountUsed >= (u32)(mem->slotCount * script_mem_slots_loadfactor);
}

static usize slot_data_size(const u32 slotCount) {
  const usize valuesSize = sizeof(ScriptVal) * slotCount;
  const usize keysSize   = sizeof(StringHash) * slotCount;
  return bits_align(valuesSize + keysSize, alignof(ScriptVal));
}

static void* slot_data_alloc(const u32 slotCount) {
  const usize memSize = slot_data_size(slotCount);
  const Mem   mem     = alloc_alloc(g_allocHeap, memSize, alignof(ScriptVal));
  mem_set(mem, 0); // Zero init all the values and keys.
  return mem.ptr;
}

static void slot_data_free(void* data, const u32 slotCount) {
  alloc_free(g_allocHeap, mem_create(data, slot_data_size(slotCount)));
}

static u32 slot_index(const void* slotData, const u32 slotCount, const StringHash key) {
  diag_assert_msg(key, "Empty memory key is not valid");

  const StringHash* keys = slot_data_keys(slotData, slotCount);

  u32 index = key & (slotCount - 1);
  for (u32 i = 0; i != slotCount; ++i) {
    if (LIKELY(!keys[index] || keys[index] == key)) {
      return index; // Slot is either empty or matches the desired key.
    }
    // Key collision, jump to a new place in the memory (quadratic probing).
    index = (index + i + 1) & (slotCount - 1);
  }
  diag_crash_msg("script: No available memory slots");
}

static void slot_data_grow(ScriptMem* mem) {
  // Allocate new slots.
  const u32   newSlotCount  = bits_nextpow2_32(mem->slotCount + 1);
  void*       newSlotData   = slot_data_alloc(newSlotCount);
  StringHash* newSlotKeys   = slot_data_keys(newSlotData, newSlotCount);
  ScriptVal*  newSlotValues = slot_data_values(newSlotData);

  StringHash* oldSlotKeys   = slot_data_keys(mem->slotData, mem->slotCount);
  ScriptVal*  oldSlotValues = slot_data_values(mem->slotData);

  // Insert the existing data into the new slots.
  for (u32 oldSlotIndex = 0; oldSlotIndex != mem->slotCount; ++oldSlotIndex) {
    const StringHash key = oldSlotKeys[oldSlotIndex];
    if (key) {
      const u32 newSlotIndex      = slot_index(newSlotData, newSlotCount, key);
      newSlotKeys[newSlotIndex]   = key;
      newSlotValues[newSlotIndex] = oldSlotValues[oldSlotIndex];
    }
  }

  // Free the old slot data.
  slot_data_free(mem->slotData, mem->slotCount);
  mem->slotData  = newSlotData;
  mem->slotCount = newSlotCount;
}

static u32 slot_insert(ScriptMem* mem, const StringHash key) {
  u32         slotIndex = slot_index(mem->slotData, mem->slotCount, key);
  StringHash* slotKeys  = slot_data_keys(mem->slotData, mem->slotCount);
  if (!slotKeys[slotIndex]) {
    // New entry; initialize and test if we need to grow the table.
    slotKeys[slotIndex] = key;
    ++mem->slotCountUsed;
    if (UNLIKELY(slot_data_should_grow(mem))) {
      slot_data_grow(mem);

      // Re-query the slot after growing the table as the previous pointer is no longer valid.
      slotIndex = slot_index(mem->slotData, mem->slotCount, key);
    }
  }
  return slotIndex;
}

ScriptMem script_mem_create(void) {
  diag_assert(bits_ispow2_32(script_mem_slots_initial));

  return (ScriptMem){
      .slotCount = script_mem_slots_initial,
      .slotData  = slot_data_alloc(script_mem_slots_initial),
  };
}

void script_mem_destroy(ScriptMem* mem) { slot_data_free(mem->slotData, mem->slotCount); }

ScriptVal script_mem_load(const ScriptMem* mem, const StringHash key) {
  const u32 slotIndex = slot_index(mem->slotData, mem->slotCount, key);
  return slot_data_values(mem->slotData)[slotIndex];
}

void script_mem_store(ScriptMem* mem, const StringHash key, const ScriptVal value) {
  const u32 slotIndex                        = slot_insert(mem, key);
  slot_data_values(mem->slotData)[slotIndex] = value;
}

ScriptMemItr script_mem_begin(const ScriptMem* mem) {
  return script_mem_next(mem, (ScriptMemItr){0});
}

ScriptMemItr script_mem_next(const ScriptMem* mem, const ScriptMemItr itr) {
  StringHash* slotKeys = slot_data_keys(mem->slotData, mem->slotCount);
  for (u32 i = itr.next; i < mem->slotCount; ++i) {
    if (slotKeys[i]) {
      return (ScriptMemItr){.key = slotKeys[i], .next = i + 1};
    }
  }
  return (ScriptMemItr){.key = 0, .next = sentinel_u32};
}
