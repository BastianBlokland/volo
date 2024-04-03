#pragma once
#include "core_string.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Handle to a loaded dynamic-library (.so / .dll).
 */
typedef struct sDynLib DynLib;

/**
 * Pointer to an exported symbol (function or variable) in a loaded dynamic-library.
 */
typedef void* DynLibSymbol;

/**
 * DynLib result-code.
 */
typedef enum {
  DynLibResult_Success = 0,
  DynLibResult_UnknownError,

  DynLibResult_Count,
} DynLibResult;

/**
 * Return a textual representation of the given DynLibResult.
 */
String dynlib_result_str(DynLibResult);

/**
 * Create a new dynamic-library handle.
 * Destroy using 'dynlib_destroy()'.
 */
DynLibResult dynlib_load(Allocator*, String path, DynLib** out);

/**
 * Destroy a dynamic-library handle.
 */
void dynlib_destroy(DynLib*);

/**
 * Lookup an exported symbol (function or variable) by name.
 */
DynLibResult dynlib_symbol(const DynLib*, String name, DynLibSymbol* out);
