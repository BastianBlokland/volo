#pragma once
#include "script_intrinsic.h"
#include "script_pos.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

// Forward declare from 'script_doc.h'.
typedef struct sScriptDoc ScriptDoc;
typedef u32               ScriptExpr;
typedef u8                ScriptVarId;

#define script_syms_max 4096
#define script_sym_sentinel sentinel_u16

typedef u16 ScriptSymId;

typedef enum {
  ScriptSymType_Keyword,
  ScriptSymType_BuiltinConstant,
  ScriptSymType_BuiltinFunction,
  ScriptSymType_ExternFunction,
  ScriptSymType_Variable,
  ScriptSymType_MemoryKey,

  ScriptSymType_Count,
} ScriptSymType;

typedef struct {
  ScriptIntrinsic intr;
} ScriptSymBuiltinFunction;

typedef struct {
  ScriptVarId slot; // NOTE: Only unique within the scope.
  ScriptRange location;
  ScriptRange scope;
} ScriptSymVariable;

typedef struct {
  StringHash key;
} ScriptSymMemoryKey;

typedef struct {
  ScriptSymType type;
  String        label;
  String        doc;
  union {
    ScriptSymBuiltinFunction builtinFunction;
    ScriptSymVariable        variable;
    ScriptSymMemoryKey       memoryKey;
  } data;
} ScriptSym;

typedef struct sScriptSymBag ScriptSymBag;

ScriptSymBag* script_sym_bag_create(Allocator*);
void          script_sym_bag_destroy(ScriptSymBag*);

ScriptSymId script_sym_push(ScriptSymBag*, const ScriptSym*);
void        script_sym_clear(ScriptSymBag*);

bool             script_sym_is_func(const ScriptSym*);
ScriptRange      script_sym_location(const ScriptSym*);
String           script_sym_type_str(ScriptSymType);
const ScriptSym* script_sym_data(const ScriptSymBag*, ScriptSymId);

ScriptSymId script_sym_find(const ScriptSymBag*, const ScriptDoc*, ScriptExpr);

ScriptSymId script_sym_first(const ScriptSymBag*, ScriptPos);
ScriptSymId script_sym_next(const ScriptSymBag*, ScriptPos, ScriptSymId);

void   script_sym_write(DynString*, String sourceText, const ScriptSym*);
String script_sym_scratch(String sourceText, const ScriptSym*);
