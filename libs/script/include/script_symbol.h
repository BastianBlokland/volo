#pragma once
#include "core_string.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

#define script_symbols_max 1024
#define script_symbol_sentinel sentinel_u16

typedef u16 ScriptSymbolId;

typedef enum {
  ScriptSymbolType_BuiltinConstant,
  ScriptSymbolType_BuiltinFunction,
} ScriptSymbolType;

typedef struct {
  ScriptSymbolType type;
  String           label;
} ScriptSymbol;

typedef struct sScriptSymbolBag ScriptSymbolBag;

ScriptSymbolBag* script_symbol_bag_create(Allocator*);
void             script_symbol_bag_destroy(ScriptSymbolBag*);

ScriptSymbolId script_symbol_push(ScriptSymbolBag*, const ScriptSymbol*);
void           script_symbol_clear(ScriptSymbolBag*);

const ScriptSymbol* script_symbol_data(const ScriptSymbolBag*, ScriptSymbolId);

ScriptSymbolId script_symbol_first(const ScriptSymbolBag*);
ScriptSymbolId script_symbol_next(const ScriptSymbolBag*, ScriptSymbolId);
