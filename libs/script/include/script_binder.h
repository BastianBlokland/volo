#pragma once
#include "script.h"

#define script_binder_max_funcs 96
#define script_binder_slot_sentinel sentinel_u16

typedef u16 ScriptBinderSlot;
typedef u64 ScriptBinderHash;

typedef struct sScriptBinderCall {
  const ScriptVal*    args;
  u32                 argCount;
  u32                 callId;
  ScriptPanicHandler* panicHandler;
} ScriptBinderCall;

typedef ScriptVal (*ScriptBinderFunc)(void* ctx, ScriptBinderCall*);

typedef enum {
  ScriptBinderFlags_None,
  ScriptBinderFlags_DisallowMemoryAccess = 1 << 0,
} ScriptBinderFlags;

/**
 * Table of native bound functions.
 */
typedef struct sScriptBinder ScriptBinder;

ScriptBinder*     script_binder_create(Allocator*, String name, ScriptBinderFlags);
void              script_binder_destroy(ScriptBinder*);
String            script_binder_name(const ScriptBinder*);
ScriptBinderFlags script_binder_flags(const ScriptBinder*);

/**
 * Set a glob filter for determining which files this binder is valid for.
 * Example: `* /units/ *.script' (NOTE: the spaces should be ignored).
 */
void   script_binder_filter_set(ScriptBinder*, String globPattern);
String script_binder_filter_get(const ScriptBinder*);
bool   script_binder_match(const ScriptBinder*, String fileIdentifier);

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
ScriptBinderSlot script_binder_slot_lookup(const ScriptBinder*, StringHash nameHash);

/**
 * Lookup the name for a slot.
 * Pre-condition: Binder has been finalized.
 */
String script_binder_slot_name(const ScriptBinder*, ScriptBinderSlot);

/**
 * Lookup the documentation for a slot.
 * Pre-condition: Binder has been finalized.
 */
String script_binder_slot_doc(const ScriptBinder*, ScriptBinderSlot);

/**
 * Lookup the signature for a slot.
 * Pre-condition: Binder has been finalized.
 */
const ScriptSig* script_binder_slot_sig(const ScriptBinder*, ScriptBinderSlot);

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
void          script_binder_write(DynString* str, const ScriptBinder*);
ScriptBinder* script_binder_read(Allocator*, String);
