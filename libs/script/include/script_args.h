#pragma once
#include "core_types.h"

// Forward declare from 'script_val.h'.
typedef union uScriptVal ScriptVal;

typedef struct {
  const ScriptVal* values;
  usize            count;
} ScriptArgs;

ScriptVal script_arg_last_or_null(ScriptArgs);
