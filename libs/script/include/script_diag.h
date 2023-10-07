#pragma once
#include "core_dynstring.h"
#include "script_error.h"
#include "script_pos.h"

#define script_diag_max 16

typedef enum {
  ScriptDiagType_Error,
  ScriptDiagType_Warning,
} ScriptDiagType;

typedef struct {
  ScriptDiagType type;
  ScriptError    error;
  ScriptPosRange range;
} ScriptDiag;

typedef struct sScriptDiagBag {
  ScriptDiag values[script_diag_max];
  u32        count;
} ScriptDiagBag;

bool script_diag_push(ScriptDiagBag*, const ScriptDiag*);
u32  script_diag_count_of_type(const ScriptDiagBag*, ScriptDiagType);
void script_diag_clear(ScriptDiagBag*);

String script_diag_msg_scratch(String sourceText, const ScriptDiag*);
void   script_diag_pretty_write(DynString*, String sourceText, const ScriptDiag*);
String script_diag_pretty_scratch(String sourceText, const ScriptDiag*);
