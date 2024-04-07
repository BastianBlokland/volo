#include "core_alloc.h"
#include "core_diag.h"
#include "core_path.h"

#include "dynlib_internal.h"

#include <dlfcn.h>
#include <limits.h>

#define dynlib_max_symbol_name 128

static bool g_dynlibInitialized;

void dynlib_pal_init() { g_dynlibInitialized = true; }

struct sDynLib {
  void*      handle;
  String     path;
  Allocator* alloc;
};

static String dynlib_err_msg() { return string_from_null_term(dlerror()); }

static String dynlib_path_query(void* handle, const String name, Allocator* alloc) {
  char dirBuffer[PATH_MAX + 1]; /* +1 for null-terminator */
  if (UNLIKELY(dlinfo(handle, RTLD_DI_ORIGIN, dirBuffer) != 0)) {
    diag_crash_msg("dlinfo() failed: {}", fmt_text(dynlib_err_msg()));
  }
  const String dir = string_from_null_term(dirBuffer);
  if (UNLIKELY(!dir.size)) {
    diag_crash_msg("Unable to find path for dynlib");
  }
  const String path = path_build_scratch(dir, name);
  return string_dup(alloc, path);
}

DynLibResult dynlib_load(Allocator* alloc, const String name, DynLib** out) {
  if (!g_dynlibInitialized) {
    diag_crash_msg("DynLib library not initialized");
  }
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
      .path   = dynlib_path_query(handle, name, alloc),
      .alloc  = alloc,
  };
  return DynLibResult_Success;
}

void dynlib_destroy(DynLib* lib) {
  if (UNLIKELY(dlclose(lib->handle) != 0)) {
    diag_crash_msg("dlclose() failed: {}", fmt_text(dynlib_err_msg()));
  }
  string_maybe_free(lib->alloc, lib->path);
  alloc_free_t(lib->alloc, lib);
}

String dynlib_path(const DynLib* lib) { return lib->path; }

DynLibSymbol dynlib_symbol(const DynLib* lib, const String name) {
  // Copy the name on the stack and null-terminate it.
  if (name.size >= dynlib_max_symbol_name) {
    diag_crash_msg("Symbol name too long");
  }
  Mem nameBuffer = mem_stack(dynlib_max_symbol_name);
  mem_cpy(nameBuffer, name);
  *mem_at_u8(nameBuffer, name.size) = '\0';

  return (DynLibSymbol)dlsym(lib->handle, nameBuffer.ptr);
}
