#pragma once
#include "core.h"
#include "core_dynarray.h"

/**
 * Owning array of characters.
 * Dynamically allocates memory when more characters get added.
 * NOTE: Any pointers / strings retrieved over DynString are invalidated on any mutating api.
 */
typedef DynArray DynString;

/**
 * Create a new dynamic string.
 * 'capacity' determines the size of the initial allocation, further allocations will be made
 * automatically when more memory is needed. 'capacity' of 0 is valid and won't allocate memory
 * until required.
 */
DynString dynstring_create(Allocator*, usize capacity);

/**
 * Create a new dynamic string over the given memory.
 * Will not allocate any memory, pushing more character then mem.size is not supported.
 */
DynString dynstring_create_over(Mem);

/**
 * Free resources held by the dynamic-string.
 */
void dynstring_destroy(DynString*);

/**
 * Retrieve the current size (in characters) of the dynamic-string.
 * NOTE: Identical to checking .size on the struct, but provided for consistency with other apis.
 */
usize dynstring_size(const DynString*);

/**
 * Retreive a string-view over the entire dynamic-string.
 * NOTE: This string is invalidated when using any of the mutating dynamic-string apis.
 */
String dynstring_view(const DynString*);

/**
 * Resizes the dynstring to be 0 length.
 */
void dynstring_clear(DynString*);
void dynstring_release(DynString*); // Also frees the underlying allocation.

/**
 * Change the size of the dynamic-string, will allocate when size is bigger then the capacity.
 */
void dynstring_resize(DynString*, usize size);

/**
 * Increase the capacity of the dynamic-string, will allocate when bigger then the current.
 */
void dynstring_reserve(DynString*, usize capacity);

/**
 * Append all characters to the end of the given dynamic-string.
 */
void dynstring_append(DynString*, String);

/**
 * Append a single character to the end of the given dynamic-string.
 */
void dynstring_append_char(DynString*, u8);

/**
 * Append 'amount' characters to the end of the given dynamic-string.
 */
void dynstring_append_chars(DynString*, u8, usize amount);

/**
 * Insert a substring at a specific index in the given dynamic-string.
 */
void dynstring_insert(DynString*, String, usize idx);

/**
 * Insert 'amount' characters at a specific index in the given dynamic-string.
 */
void dynstring_insert_chars(DynString*, u8, usize idx, usize amount);

/**
 * Remove 'amount' characters from the given dynamic-string.
 * NOTE: characters being single byte values, not Unicode codepoints.
 */
void dynstring_erase_chars(DynString*, usize idx, usize amount);

/**
 * Append 'amount' empty space at the end of the dynamic-string.
 * NOTE: the new space is NOT initialized and its up to the caller to write to it.
 */
String dynstring_push(DynString*, usize amount);

/**
 * Replace any occurrence of 'text' with 'replacement'.
 */
void dynstring_replace(DynString*, String text, String replacement);
