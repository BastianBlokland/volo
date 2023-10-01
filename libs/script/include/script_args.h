#pragma once
#include "core_types.h"

// Forward declare from 'script_val.h'.
typedef union uScriptVal ScriptVal;

typedef struct {
  const ScriptVal* values;
  usize            count;
} ScriptArgs;
