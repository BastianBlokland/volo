#pragma once
#include "core_dynstring.h"
#include "script_pos.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

#define script_syms_max 1024
#define script_sym_sentinel sentinel_u16

typedef u16 ScriptSymId;

typedef enum {
  ScriptSymType_Keyword,
  ScriptSymType_BuiltinConstant,
  ScriptSymType_BuiltinFunction,
  ScriptSymType_ExternFunction,

  ScriptSymType_Count,
} ScriptSymType;

typedef struct {
  ScriptSymType  type;
  String         label;
  ScriptPosRange validRange;
} ScriptSym;

typedef struct sScriptSymBag ScriptSymBag;

ScriptSymBag* script_sym_bag_create(Allocator*);
void          script_sym_bag_destroy(ScriptSymBag*);

ScriptSymId script_sym_push(ScriptSymBag*, const ScriptSym*);
void        script_sym_clear(ScriptSymBag*);

String           script_sym_type_str(ScriptSymType);
const ScriptSym* script_sym_data(const ScriptSymBag*, ScriptSymId);

ScriptSymId script_sym_first(const ScriptSymBag*, ScriptPos);
ScriptSymId script_sym_next(const ScriptSymBag*, ScriptPos, ScriptSymId);

void   script_sym_write(DynString*, const ScriptSym*);
String script_sym_scratch(const ScriptSym*);
