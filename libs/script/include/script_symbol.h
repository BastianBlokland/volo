#pragma once

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

typedef enum {
  ScriptSymbolType_BuiltinConstant,
  ScriptSymbolType_BuiltinFunction,
} ScriptSymbolType;

typedef struct {
  ScriptSymbolType type;
} ScriptSymbol;

typedef struct sScriptSymbolBag ScriptSymbolBag;

ScriptSymbolBag* script_symbol_bag_create(Allocator*);
void             script_symbol_bag_destroy(ScriptSymbolBag*);
