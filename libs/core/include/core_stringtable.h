#pragma once
#include "core_string.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Table for storing the textual representation of StringHash values, often useful for visualization
 * purposes.
 * NOTE: Meant for storing short strings, preferably less then 128 bytes.
 */
typedef struct sStringTable StringTable;

/**
 * Create a new StringTable instance.
 * Destroy using 'stringtable_destroy()'.
 */
StringTable* stringtable_create(Allocator*);

/**
 * Destroy a StringTable instance.
 */
void stringtable_destroy(StringTable*);

/**
 * Lookup a textual representation of the given hash.
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
void stringtable_add(StringTable*, String);
