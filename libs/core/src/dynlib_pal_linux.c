#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"

#include <dlfcn.h>
#include <limits.h>

#define dynlib_max_symbol_name 128

void dynlib_init() {}

struct sDynLib {
  void*      handle;
  Allocator* alloc;
};

static String dynlib_err_msg() { return string_from_null_term(dlerror()); }

DynLibResult dynlib_load(Allocator* alloc, const String name, DynLib** out) {
  // Copy the name on the stack and null-terminate it.
  if (name.size >= PATH_MAX) {
    return DynLibResult_LibraryNameTooLong;
  }
  Mem nameBuffer = mem_stack(PATH_MAX);
  mem_cpy(nameBuffer, name);
  *mem_at_u8(nameBuffer, name.size) = '\0';

  void* handle = dlopen(name.ptr, RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    return DynLibResult_LibraryNotFound;
  }

  *out  = alloc_alloc_t(alloc, DynLib);
  **out = (DynLib){
      .handle = handle,
      .alloc  = alloc,
  };
  return DynLibResult_Success;
}

void dynlib_destroy(DynLib* lib) {
  if (UNLIKELY(dlclose(lib->handle) != 0)) {
    diag_crash_msg("dlclose() failed: {}", fmt_text(dynlib_err_msg()));
  }
  alloc_free_t(lib->alloc, lib);
}

DynLibResult dynlib_symbol(const DynLib* lib, const String name, DynLibSymbol* out) {
  // Copy the name on the stack and null-terminate it.
  if (name.size >= dynlib_max_symbol_name) {
    return DynLibResult_SymbolNameTooLong;
  }
  Mem nameBuffer = mem_stack(dynlib_max_symbol_name);
  mem_cpy(nameBuffer, name);
  *mem_at_u8(nameBuffer, name.size) = '\0';

  void* sym = dlsym(lib->handle, nameBuffer.ptr);
  if (!sym) {
    return DynLibResult_SymbolNotFound;
  }

  *out = (DynLibSymbol)sym;
  return DynLibResult_Success;
}
