#pragma once
#include "core_string.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

typedef u32 ScriptBindSlot;

/**
 * Table of native functions to bind.
 */
typedef struct sScriptBinder ScriptBinder;

/**
 * Create a new ScriptBinder instance.
 * Destroy using 'script_mem_destroy()'.
 */
ScriptBinder* script_binder_create(Allocator*);

/**
 * Destroy a ScriptBinder instance.
 */
void script_binder_destroy(ScriptBinder*);

/**
 * Declare a function to bind.
 * Pre-condition: Binder has not been prepared.
 */
void script_binder_declare(ScriptBinder*, StringHash name);

/**
 * Prepare the binder for linking.
 * NOTE: No more declarations can be made after calling this.
 * Pre-condition: Binder has not been prepared.
 */
void script_binder_prepare(ScriptBinder*);

/**
 * Lookup a declaration by name.
 * NOTE: Returns sentinel_u32 if no declaration was found with the given name.
 * Pre-condition: Binder has been prepared.
 */
ScriptBindSlot script_binder_lookup(const ScriptBinder*, StringHash name);
