#pragma once

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

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
