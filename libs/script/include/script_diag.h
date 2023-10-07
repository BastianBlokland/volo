#pragma once
#include "core_dynstring.h"
#include "script_pos.h"
#include "script_result.h"

#define script_diag_max 8

typedef struct {
  ScriptResult   error;
  ScriptPosRange range;
} ScriptDiag;

typedef struct sScriptDiagBag {
  ScriptDiag diagnostics[script_diag_max];
  u32        count;
} ScriptDiagBag;

bool script_diag_push(ScriptDiagBag*, const ScriptDiag*);
void script_diag_clear(ScriptDiagBag*);

void   script_diag_write(DynString*, String sourceText, const ScriptDiag*);
String script_diag_scratch(String sourceText, const ScriptDiag*);
