#pragma once
#include "core_string.h"

/**
 * Handle to a loaded dynamic-library (.so / .dll).
 */
typedef struct sDynLib DynLib;

/**
 * DynLib result-code.
 */
typedef enum {
  DynLibResult_Success = 0,
  DynLibResult_LibraryNameTooLong,
  DynLibResult_LibraryNotFound,
  DynLibResult_UnknownError,

  DynLibResult_Count,
} DynLibResult;

/**
 * Return a textual representation of the given DynLibResult.
 */
String dynlib_result_str(DynLibResult);

/**
 * Create a new dynamic-library handle.
 * NOTE: name can either be an absolute path to a .so / .dll or a path relative to the search paths.
 * Destroy using 'dynlib_destroy()'.
 */
DynLibResult dynlib_load(Allocator*, String name, DynLib** out);
DynLibResult dynlib_load_first(Allocator*, const String names[], u32 nameCount, DynLib** out);

/**
 * Destroy a dynamic-library handle.
 * NOTE: If this the last handle to the library it will be unloaded and invalidate retrieved syms.
 */
void dynlib_destroy(DynLib*);

/**
 * Lookup the path of the given dynamic-library.
 */
String dynlib_path(const DynLib*);

/**
 * Lookup an exported symbol (function or variable) by name.
 * NOTE: Returns null if the symbol could not be found.
 */
Symbol dynlib_symbol(const DynLib*, String name);

/**
 * Returns the amount of currently loaded libraries.
 */
u32 dynlib_count(void);
