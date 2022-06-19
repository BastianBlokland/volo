#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_stringtable.h"

#define stringtable_chunk_size (32 * usize_kibibyte)
#define stringtable_string_size_max 512
#define stringtable_slots_initial 128
#define stringtable_slots_loadfactor 0.75f

/**
 * Strings are looked up using a simple open-addressing hash table.
 * https://en.wikipedia.org/wiki/Open_addressing
 *
 * NOTE: Strings cannot be removed from the table at this time.
 */

typedef struct {
  StringHash hash;
  String     data;
} StringTableSlot;

struct sStringTable {
  Allocator*       alloc; // Allocator for string the meta-data.
  u32              slotCount, slotCountUsed;
  StringTableSlot* slots;
  Allocator*       dataAlloc; // Allocator for string the string character data.
};

static u32 stringtable_should_grow(StringTable* table) {
  return table->slotCountUsed >= (u32)(table->slotCount * stringtable_slots_loadfactor);
}

static StringTableSlot* stringtable_slots_alloc(Allocator* alloc, const u32 slotCount) {
  const usize slotsMemSize = sizeof(StringTableSlot) * slotCount;
  const Mem   slotsMem     = alloc_alloc(alloc, slotsMemSize, alignof(StringTableSlot));
  mem_set(slotsMem, 0);
  return slotsMem.ptr;
}

static StringTableSlot*
stringtable_slot(StringTableSlot* slots, const u32 slotCount, const StringHash hash) {
  diag_assert(hash); // Hash of 0 is invalid.

  u32 bucket = hash & (slotCount - 1);
  for (usize i = 0; i != slotCount; ++i) {
    StringTableSlot* slot = &slots[bucket];
    if (LIKELY(!slot->hash || slot->hash == hash)) {
      return slot; // Slot is either empty or the desired hash.
    }
    // Hash collision, jump to a new place in the table (quadratic probing).
    bucket = (bucket + i + 1) & (slotCount - 1);
  }
  diag_crash_msg("No available StringTable slots");
}

static void stringtable_grow(StringTable* table) {
  // Allocate new slots.
  const u32        newSlotCount = bits_nextpow2_32(table->slotCount + 1);
  StringTableSlot* newSlots     = stringtable_slots_alloc(table->alloc, newSlotCount);

  // Insert the old data into the new slots.
  for (StringTableSlot* slot = table->slots; slot != (table->slots + table->slotCount); ++slot) {
    if (slot->hash) {
      StringTableSlot* newSlot = stringtable_slot(newSlots, newSlotCount, slot->hash);
      newSlot->hash            = slot->hash;
      newSlot->data            = slot->data;
    }
  }

  // Free the old slots.
  alloc_free_array_t(table->alloc, table->slots, table->slotCount);
  table->slots     = newSlots;
  table->slotCount = newSlotCount;
}

StringTable* stringtable_create(Allocator* alloc) {
  StringTable* table = alloc_alloc_t(alloc, StringTable);

  diag_assert(bits_ispow2_32(stringtable_slots_initial));

  *table = (StringTable){
      .alloc     = alloc,
      .slotCount = stringtable_slots_initial,
      .slots     = stringtable_slots_alloc(alloc, stringtable_slots_initial),
      .dataAlloc = alloc_chunked_create(g_alloc_page, alloc_bump_create, stringtable_chunk_size),
  };
  return table;
}

void stringtable_destroy(StringTable* table) {
  alloc_free_array_t(table->alloc, table->slots, table->slotCount);
  alloc_chunked_destroy(table->dataAlloc);
  alloc_free_t(table->alloc, table);
}

String stringtable_lookup(const StringTable* table, const StringHash hash) {
  return stringtable_slot(table->slots, table->slotCount, hash)->data;
}

void stringtable_add(StringTable* table, const String str) {
  diag_assert_msg(
      str.size <= stringtable_string_size_max,
      "String size '{}' exceeds maximum",
      fmt_size(str.size));

  const StringHash hash = string_hash(str);
  StringTableSlot* slot = stringtable_slot(table->slots, table->slotCount, hash);
  if (slot->hash) {
    /**
     * String already existed in the table.
     */
    diag_assert_msg(string_eq(str, slot->data), "StringHash collision in StringTable");
  } else {
    /**
     * New entry in the table.
     * Copy the string data into the table's data-allocator and initial the values in the slot.
     */
    slot->hash = hash;
    if (LIKELY(!string_is_empty(str))) {
      slot->data = string_dup(table->dataAlloc, str);
      diag_assert_msg(slot->data.ptr, "StringTable allocator ran out of space");
    }
    ++table->slotCountUsed;
    if (UNLIKELY(stringtable_should_grow(table))) {
      stringtable_grow(table);
    }
  }
}
