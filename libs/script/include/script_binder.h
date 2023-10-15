#pragma once
#include "core_string.h"
#include "script_args.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'script_val.h'.
typedef union uScriptVal ScriptVal;

#define script_binder_slot_sentinel sentinel_u32

typedef u32 ScriptBinderSlot;
typedef u64 ScriptBinderSignature;

typedef ScriptVal (*ScriptBinderFunc)(void* ctx, ScriptArgs);

/**
 * Table of native bound functions.
 */
typedef struct sScriptBinder ScriptBinder;

/**
 * Create a new ScriptBinder instance.
 * Destroy using 'script_binder_destroy()'.
 */
ScriptBinder* script_binder_create(Allocator*);

/**
 * Destroy a ScriptBinder instance.
 */
void script_binder_destroy(ScriptBinder*);

/**
 * Declare a new function.
 * NOTE: Passing a null function is supported if the binder is only used for lookups.
 * Pre-condition: Binder has not been finalized.
 */
void script_binder_declare(ScriptBinder*, StringHash name, ScriptBinderFunc);

/**
 * Finalize the binder for lookups and execution.
 * NOTE: No more bindings can be added after calling this.
 * Pre-condition: Binder has not been finalized.
 */
void script_binder_finalize(ScriptBinder*);

/**
 * Return the binding count.
 * Pre-condition: Binder has been finalized.
 */
u32 script_binder_count(const ScriptBinder*);

/**
 * Compute a signature for the binder.
 * Binders with the same signature are compatible.
 * Pre-condition: Binder has been finalized.
 */
ScriptBinderSignature script_binder_sig(const ScriptBinder*);

/**
 * Lookup a function by name.
 * NOTE: Returns 'script_binder_slot_sentinel' if no function was found with the given name.
 * Pre-condition: Binder has been finalized.
 */
ScriptBinderSlot script_binder_lookup(const ScriptBinder*, StringHash name);

/**
 * Iterate over the bound slots.
 * Pre-condition: Binder has been finalized.
 */
ScriptBinderSlot script_binder_first(const ScriptBinder*);
ScriptBinderSlot script_binder_next(const ScriptBinder*, ScriptBinderSlot);

/**
 * Execute a bound function.
 * Pre-condition: Binder has been finalized.
 */
ScriptVal script_binder_exec(const ScriptBinder*, ScriptBinderSlot func, void* ctx, ScriptArgs);
