#pragma once
#include "core_string.h"
#include "script.h"

#define script_sig_arg_count_max 10
#define script_sig_arg_name_max 64

typedef enum {
  ScriptSigArgFlags_None  = 0,
  ScriptSigArgFlags_Multi = 1 << 0,
} ScriptSigArgFlags;

typedef struct sScriptSigArg {
  String            name;
  ScriptMask        mask; // Mask of accepted types.
  ScriptSigArgFlags flags;
} ScriptSigArg;

typedef struct sScriptSig ScriptSig;

ScriptSig* script_sig_create(Allocator*, ScriptMask ret, const ScriptSigArg args[], u8 argCount);
ScriptSig* script_sig_clone(Allocator*, const ScriptSig*);
void       script_sig_destroy(ScriptSig*, Allocator*);

ScriptMask   script_sig_ret(const ScriptSig*);
u8           script_sig_arg_count(const ScriptSig*);
ScriptSigArg script_sig_arg(const ScriptSig* sig, u8 index);

u8 script_sig_arg_min_count(const ScriptSig*);
u8 script_sig_arg_max_count(const ScriptSig*);

void   script_sig_arg_write(const ScriptSig*, u8 index, DynString*);
String script_sig_arg_scratch(const ScriptSig*, u8 index);

void   script_sig_write(const ScriptSig*, DynString*);
String script_sig_scratch(const ScriptSig*);
