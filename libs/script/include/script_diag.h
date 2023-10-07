#pragma once
#include "core_dynstring.h"
#include "script_error.h"
#include "script_pos.h"

#define script_diag_max 8

typedef struct {
  ScriptError    error;
  ScriptPosRange range;
} ScriptDiag;

typedef struct sScriptDiagBag {
  ScriptDiag values[script_diag_max];
  u32        count;
} ScriptDiagBag;

bool script_diag_push(ScriptDiagBag*, const ScriptDiag*);
void script_diag_clear(ScriptDiagBag*);
bool script_diag_any_error(const ScriptDiagBag*);

String script_diag_msg_scratch(String sourceText, const ScriptDiag*);

void   script_diag_write(DynString*, String sourceText, const ScriptDiag*);
String script_diag_scratch(String sourceText, const ScriptDiag*);
