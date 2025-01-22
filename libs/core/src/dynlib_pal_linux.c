#include "core_alloc.h"
#include "core_diag.h"
#include "core_path.h"

#include "dynlib_internal.h"

#include <dlfcn.h>
#include <limits.h>

#define dynlib_max_symbol_name 128
#define dynlib_crash_on_error false

void dynlib_pal_init(void) {}
void dynlib_pal_teardown(void) {}

struct sDynLib {
  void*      handle;
  String     path;
  Allocator* alloc;
};

static String dynlib_err_msg(void) {
  const char* msg = dlerror();
  return msg ? string_from_null_term(msg) : string_lit("Unknown error");
}

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

DynLibResult dynlib_pal_load(Allocator* alloc, const String name, DynLib** out) {
  // Copy the name on the stack and null-terminate it.
  if (name.size >= PATH_MAX) {
    return DynLibResult_LibraryNameTooLong;
  }
  Mem nameBuffer = mem_stack(PATH_MAX);
  mem_cpy(nameBuffer, name);
  *mem_at_u8(nameBuffer, name.size) = '\0';

  void* handle = dlopen(name.ptr, RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
#if dynlib_crash_on_error
    diag_crash_msg("dynlib_load('{}'): {}", fmt_text(name), fmt_text(dynlib_err_msg()));
#endif
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

void dynlib_pal_destroy(DynLib* lib) {
  if (UNLIKELY(dlclose(lib->handle) != 0)) {
    diag_crash_msg("dlclose() failed: {}", fmt_text(dynlib_err_msg()));
  }
  string_maybe_free(lib->alloc, lib->path);
  alloc_free_t(lib->alloc, lib);
}

String dynlib_pal_path(const DynLib* lib) { return lib->path; }

Symbol dynlib_pal_symbol(const DynLib* lib, const String name) {
  // Copy the name on the stack and null-terminate it.
  if (name.size >= dynlib_max_symbol_name) {
    diag_crash_msg("Symbol name too long");
  }
  Mem nameBuffer = mem_stack(dynlib_max_symbol_name);
  mem_cpy(nameBuffer, name);
  *mem_at_u8(nameBuffer, name.size) = '\0';

  Symbol sym = (Symbol)dlsym(lib->handle, nameBuffer.ptr);
#if dynlib_crash_on_error
  if (UNLIKELY(!sym)) {
    diag_crash_msg("dynlib_symbol('{}'): {}", fmt_text(name), fmt_text(dynlib_err_msg()));
  }
#endif
  return sym;
}
