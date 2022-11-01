#pragma once
#include "script_val.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Memory instance for storing values.
 */
typedef struct sScriptMem ScriptMem;

/**
 * Create a new ScriptMem instance.
 * Destroy using 'script_mem_destroy()'.
 */
ScriptMem* script_mem_create(Allocator*);

/**
 * Destroy a ScriptMem instance.
 */
void script_mem_destroy(ScriptMem*);

/**
 * Query and update values.
 * Pre-condition: key != 0.
 */
ScriptVal script_mem_get(const ScriptMem*, StringHash key);
void      script_mem_set(ScriptMem*, StringHash key, ScriptVal);
void      script_mem_set_null(ScriptMem*, StringHash key);

/**
 * Iterator for iterating memory keys.
 * NOTE: Iterator is invalidated when new entries are inserted.
 */
typedef struct {
  StringHash key; // '0' indicates that no more keys are found.
  u32        next;
} ScriptMemItr;

ScriptMemItr script_mem_begin(const ScriptMem*);
ScriptMemItr script_mem_next(const ScriptMem*, ScriptMemItr);
