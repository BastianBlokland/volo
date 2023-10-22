#pragma once
#include "script_pos.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

typedef enum {
  ScriptPanic_None,
  ScriptPanic_AssertionFailed,
  ScriptPanic_ExecutionLimitExceeded,

  ScriptPanicType_Count,
} ScriptPanicType;

typedef struct {
  ScriptPanicType type;
  ScriptRange     range;
} ScriptPanic;

bool   script_panic_valid(const ScriptPanic*);
void   script_panic_pretty_write(DynString*, String sourceText, const ScriptPanic*);
String script_panic_pretty_scratch(String sourceText, const ScriptPanic*);
