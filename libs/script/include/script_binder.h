#pragma once
#include "core_string.h"
#include "script_error.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

// Forward declare from 'script_val.h'.
typedef struct sScriptVal ScriptVal;

// Forward declare from 'script_sig.h'.
typedef struct sScriptSig ScriptSig;

#define script_binder_slot_sentinel sentinel_u16

typedef u16 ScriptBinderSlot;
typedef u64 ScriptBinderHash;

typedef struct {
  const ScriptVal* args;
  u32              argCount;
  u32              callId;
  ScriptError      err;
} ScriptBinderCall;

typedef ScriptVal (*ScriptBinderFunc)(void* ctx, ScriptBinderCall*);

/**
 * Table of native bound functions.
 */
typedef struct sScriptBinder ScriptBinder;

ScriptBinder* script_binder_create(Allocator*);
void          script_binder_destroy(ScriptBinder*);

/**
 * Declare a new function.
 * NOTE: Passing a null function is supported if the binder is only used for lookups.
 * Pre-condition: Binder has not been finalized.
 */
void script_binder_declare(
    ScriptBinder*, String name, String doc, const ScriptSig* sig, ScriptBinderFunc);

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
u16 script_binder_count(const ScriptBinder*);

/**
 * Compute a hash for the binder.
 * Binders with the same hash are compatible.
 * Pre-condition: Binder has been finalized.
 */
ScriptBinderHash script_binder_hash(const ScriptBinder*);

/**
 * Lookup a function by name.
 * NOTE: Returns 'script_binder_slot_sentinel' if no function was found with the given name.
 * Pre-condition: Binder has been finalized.
 */
ScriptBinderSlot script_binder_lookup(const ScriptBinder*, StringHash nameHash);

/**
 * Lookup the name for a slot.
 * Pre-condition: Binder has been finalized.
 */
String script_binder_name(const ScriptBinder*, ScriptBinderSlot);

/**
 * Lookup the documentation for a slot.
 * Pre-condition: Binder has been finalized.
 */
String script_binder_doc(const ScriptBinder*, ScriptBinderSlot);

/**
 * Lookup the signature for a slot.
 * Pre-condition: Binder has been finalized.
 */
const ScriptSig* script_binder_sig(const ScriptBinder*, ScriptBinderSlot);

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
ScriptVal script_binder_exec(const ScriptBinder*, ScriptBinderSlot, void* ctx, ScriptBinderCall*);

/**
 * Binder serialization utils.
 */
void script_binder_write(DynString* str, const ScriptBinder*);
bool script_binder_read(ScriptBinder*, String);
