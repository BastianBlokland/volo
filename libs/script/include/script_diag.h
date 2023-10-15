#pragma once
#include "core_dynstring.h"
#include "script_error.h"
#include "script_pos.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

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

typedef struct sScriptDiagBag ScriptDiagBag;

ScriptDiagBag* script_diag_bag_create(Allocator*);
void           script_diag_bag_destroy(ScriptDiagBag*);

const ScriptDiag* script_diag_data(const ScriptDiagBag*);
u32               script_diag_count(const ScriptDiagBag*);
u32               script_diag_count_of_type(const ScriptDiagBag*, ScriptDiagType);
const ScriptDiag* script_diag_first_of_type(const ScriptDiagBag*, ScriptDiagType);

bool script_diag_push(ScriptDiagBag*, const ScriptDiag*);
void script_diag_clear(ScriptDiagBag*);

String script_diag_msg_scratch(String sourceText, const ScriptDiag*);
void   script_diag_pretty_write(DynString*, String sourceText, const ScriptDiag*);
String script_diag_pretty_scratch(String sourceText, const ScriptDiag*);
