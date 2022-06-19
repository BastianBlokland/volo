#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_stringtable.h"

#define stringtable_chunk_size (64 * usize_kibibyte)
#define stringtable_string_size_max 512
#define stringtable_initial_size 128

typedef struct {
  StringHash hash;
  String     data;
} StringTableSlot;

struct sStringTable {
  Allocator*       alloc; // Allocator for string the meta-data.
  u32              tableSize;
  StringTableSlot* slots;
  Allocator*       dataAlloc; // Allocator for string the string character data.
};

static StringTableSlot* stringtable_lookup_slot(StringTable* table, const StringHash hash) {
  diag_assert(hash); // Hash of 0 is invalid.

  u32 bucket = hash & (table->tableSize - 1);
  for (usize i = 0; i != table->tableSize; ++i) {
    StringTableSlot* slot = &table->slots[bucket];
    if (LIKELY(!slot->hash || slot->hash == hash)) {
      return slot; // Slot is either empty or the desired hash.
    }
    // Hash collision, jump to a new place in the table (quadratic probing).
    bucket = (bucket + i + 1) & (table->tableSize - 1);
  }
  diag_crash_msg("StringTable is full");
}

StringTable* stringtable_create(Allocator* alloc) {
  StringTable* table = alloc_alloc_t(alloc, StringTable);

  const usize slotsMemSize = sizeof(StringTableSlot) * stringtable_initial_size;
  const Mem   slotsMem     = alloc_alloc(alloc, slotsMemSize, alignof(StringTableSlot));
  mem_set(slotsMem, 0);

  *table = (StringTable){
      .alloc     = alloc,
      .tableSize = stringtable_initial_size,
      .slots     = slotsMem.ptr,
      .dataAlloc = alloc_chunked_create(g_alloc_page, alloc_bump_create, stringtable_chunk_size),
  };
  return table;
}

void stringtable_destroy(StringTable* table) {
  alloc_free_array_t(table->alloc, table->slots, table->tableSize);
  alloc_chunked_destroy(table->dataAlloc);
  alloc_free_t(table->alloc, table);
}

String stringtable_lookup(const StringTable* table, const StringHash hash) {
  return stringtable_lookup_slot((StringTable*)table, hash)->data;
}

void stringtable_add(StringTable* table, const String str) {
  diag_assert_msg(
      str.size <= stringtable_string_size_max,
      "String size '{}' exceeds maximum",
      fmt_size(str.size));

  const StringHash hash = string_hash(str);
  StringTableSlot* slot = stringtable_lookup_slot(table, hash);
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
    if (!string_is_empty(str)) {
      slot->data = string_dup(table->dataAlloc, str);
      diag_assert_msg(slot->data.ptr, "StringTable allocator ran out of space");
    }
  }
}
