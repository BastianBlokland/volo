#pragma once
#include "core_string.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

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

void script_symbol_push(ScriptSymbolBag*, const ScriptSymbol*);
void script_symbol_clear(ScriptSymbolBag*);
