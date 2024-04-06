#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_winutils.h"

#include <windows.h>

#define dynlib_max_symbol_name 128

void dynlib_init() {
  /**
   * Disable Windows ui error popups that could be shown as a result of calling 'LoadLibrary'.
   */
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
}

struct sDynLib {
  HMODULE    handle;
  String     path;
  Allocator* alloc;
};

static String dynlib_path_query(HMODULE handle, Allocator* alloc) {
  Mem widePathBuffer       = mem_stack((MAX_PATH + 1) * sizeof(wchar_t)); // +1 for null-terminator.
  const usize widePathSize = GetModuleFileName(handle, widePathBuffer.ptr, MAX_PATH);
  if (!widePathSize) {
    const DWORD err = GetLastError();
    diag_crash_msg(
        "GetModuleFileName() failed: {}, {}",
        fmt_int((u64)err),
        fmt_text(winutils_error_msg_scratch(err)));
  }
  const String str = winutils_from_widestr_scratch(widePathBuffer.ptr, widePathSize);
  return string_dup(alloc, str);
}

DynLibResult dynlib_load(Allocator* alloc, const String name, DynLib** out) {
  // Convert the path to a null-terminated wide-char string.
  const usize pathBufferSize = winutils_to_widestr_size(name);
  if (pathBufferSize >= MAX_PATH) {
    return DynLibResult_LibraryNameTooLong;
  }
  Mem pathBufferMem = mem_stack(pathBufferSize);
  winutils_to_widestr(pathBufferMem, name);

  HMODULE handle = LoadLibrary(pathBufferMem.ptr);
  if (!handle) {
    return DynLibResult_LibraryNotFound;
  }

  *out  = alloc_alloc_t(alloc, DynLib);
  **out = (DynLib){
      .handle = handle,
      .path   = dynlib_path_query(handle, alloc),
      .alloc  = alloc,
  };
  return DynLibResult_Success;
}

void dynlib_destroy(DynLib* lib) {
  if (UNLIKELY(FreeLibrary(lib->handle) == 0)) {
    const DWORD err = GetLastError();
    diag_crash_msg(
        "FreeLibrary() failed: {}, {}",
        fmt_int((u64)err),
        fmt_text(winutils_error_msg_scratch(err)));
  }
  string_maybe_free(lib->alloc, lib->path);
  alloc_free_t(lib->alloc, lib);
}

String dynlib_path(const DynLib* lib) { return lib->path; }

DynLibResult dynlib_symbol(const DynLib* lib, const String name, DynLibSymbol* out) {
  // Copy the name on the stack and null-terminate it.
  if (name.size >= dynlib_max_symbol_name) {
    return DynLibResult_SymbolNameTooLong;
  }
  Mem nameBuffer = mem_stack(dynlib_max_symbol_name);
  mem_cpy(nameBuffer, name);
  *mem_at_u8(nameBuffer, name.size) = '\0';

  FARPROC sym = GetProcAddress(lib->handle, nameBuffer.ptr);
  if (!sym) {
    return DynLibResult_SymbolNotFound;
  }

  *out = (DynLibSymbol)sym;
  return DynLibResult_Success;
}
