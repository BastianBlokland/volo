#pragma once
#include "core_string.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'script_val.h'.
typedef u16 ScriptMask;

#define script_sig_arg_count_max 10
#define script_sig_arg_name_max 64

typedef struct {
  String     name;
  ScriptMask mask; // Mask of accepted types.
} ScriptSigArg;

typedef struct sScriptSig ScriptSig;

ScriptSig* script_sig_create(Allocator*, ScriptMask ret, const ScriptSigArg args[], u8 argCount);
ScriptSig* script_sig_clone(Allocator*, ScriptSig*);
void       script_sig_destroy(ScriptSig*);

ScriptMask   script_sig_ret(const ScriptSig*);
u8           script_sig_arg_count(const ScriptSig*);
ScriptSigArg script_sig_arg(const ScriptSig* sig, u8 index);
