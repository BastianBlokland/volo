#pragma once
#include "core_dynarray.h"
#include "core_string.h"

/**
 * Owning array of characters.
 * Dynamically allocates memory when more characters get added.
 * Note: Any pointers / strings retreived over DynString are invalidated on any mutating api.
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
 * Free resources held by the dynamic-string.
 */
void dynstring_destroy(DynString*);

/**
 * Retreive a string-view over the entire dynamic-string.
 * Note: This string is invalidated when using any of the mutating dynamic-string apis.
 */
String dynstring_view(const DynString*);

/**
 * Append all characters of the given string to the end of this dynamic-string/
 */
void dynstring_append(DynString*, String);
