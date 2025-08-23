#pragma once
#include "core/string.h"

/**
 * Table for storing strings.
 * NOTE: Meant for storing short strings, preferably less then 128 bytes.
 */
typedef struct sStringTable StringTable;

/**
 * Global StringTable.
 * NOTE: Thread-safe.
 */
extern StringTable* g_stringtable;

/**
 * Create a new StringTable instance.
 * Destroy using 'stringtable_destroy()'.
 */
StringTable* stringtable_create(Allocator*);

/**
 * Destroy a StringTable instance.
 */
void stringtable_destroy(StringTable*);
void stringtable_reset(StringTable*);

/**
 * Lookup the amount of strings in the given StringTable.
 */
u32 stringtable_count(const StringTable*);

/**
 * Lookup a String by hash.
 * NOTE: If the hash has not been added to the table an empty String is returned.
 * NOTE: Thread-safe.
 */
String stringtable_lookup(const StringTable*, StringHash);

/**
 * Add the given string to the StringTable.
 * NOTE: This is a no-op if the string is already in the table.
 * NOTE: Thread-safe.
 * Pre-condition: string.size <= 512
 */
StringHash stringtable_add(StringTable*, String);

/**
 * Store a copy of the given string in the StringTable, the returned pointer is stable throughout
 * the table's lifetime.
 * NOTE: Strings are deduplicated: returns an existing string if one matches.
 * NOTE: Thread-safe.
 * Pre-condition: string.size <= 512
 */
String stringtable_intern(StringTable*, String);

typedef struct {
  String* values;
  usize   count;
} StringTableArray;

/**
 * Clone the strings in the given StringTable to a new heap-array.
 * NOTE: The individual strings are NOT heap allocated, instead they are interned in the table.
 */
StringTableArray stringtable_clone_strings(const StringTable*, Allocator*);
