#pragma once
#include "core_types.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Definition of a Script Document for storing script expressions.
 */
typedef struct sScriptDoc ScriptDoc;

/**
 * Create a new Script document.
 * NOTE: 'exprCapacity' is only the initial capacity, more space is automatically allocated when
 * required. Capacity of 0 is legal and will allocate memory when the first expression is added.
 *
 * Should be destroyed using 'script_destroy()'.
 */
ScriptDoc* script_create(Allocator*, usize exprCapacity);

/**
 * Destroy a Script document.
 */
void script_destroy(ScriptDoc*);
