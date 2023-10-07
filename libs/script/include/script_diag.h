#pragma once
#include "core_dynstring.h"
#include "script_pos.h"
#include "script_result.h"

typedef struct {
  ScriptResult   error;
  ScriptPosRange range;
} ScriptDiag;

void   script_diag_write(DynString*, String sourceText, const ScriptDiag*);
String script_diag_scratch(String sourceText, const ScriptDiag*);
