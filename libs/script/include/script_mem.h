#pragma once
#include "script.h"

/**
 * Memory instance for storing values.
 */
typedef struct sScriptMem {
  u32   slotCount, slotCountUsed;
  void* slotData;
} ScriptMem;

/**
 * Create a new ScriptMem instance.
 * Destroy using 'script_mem_destroy()'.
 */
ScriptMem script_mem_create(void);

/**
 * Destroy a ScriptMem instance.
 */
void script_mem_destroy(ScriptMem*);

/**
 * Query and update values.
 * Pre-condition: key != 0.
 */
ScriptVal script_mem_load(const ScriptMem*, StringHash key);
void      script_mem_store(ScriptMem*, StringHash key, ScriptVal);

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
