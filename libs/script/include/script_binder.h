#pragma once
#include "core_string.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'script_val.h'.
typedef union uScriptVal ScriptVal;

typedef u32 ScriptBinderSlot;
typedef u64 ScriptBinderSignature;

typedef ScriptVal (*ScriptBinderFunc)(void* ctx, const ScriptVal* args, usize argCount);

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
 * Bind a new function.
 * NOTE: Passing a null function is supported if the binder is only used for lookups.
 * Pre-condition: Binder has not been build.
 */
void script_binder_bind(ScriptBinder*, StringHash name, ScriptBinderFunc);

/**
 * Build the binder for lookups and execution.
 * NOTE: No more bindings can be added after calling this.
 * Pre-condition: Binder has not been build.
 */
void script_binder_build(ScriptBinder*);

/**
 * Compute a signature for the binder.
 * Binders with the same signature are compatible.
 * Pre-condition: Binder has been build.
 */
ScriptBinderSignature script_binder_sig(const ScriptBinder*);

/**
 * Lookup a function by name.
 * NOTE: Returns sentinel_u32 if no function was found with the given name.
 * Pre-condition: Binder has been build.
 */
ScriptBinderSlot script_binder_lookup(const ScriptBinder*, StringHash name);

/**
 * Execute a bound function.
 * Pre-condition: Binder has been build.
 */
ScriptVal script_binder_exec(
    const ScriptBinder*, ScriptBinderSlot func, void* ctx, const ScriptVal* args, usize argCount);
