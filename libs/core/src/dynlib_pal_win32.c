#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "core_winutils.h"

#include "dynlib_internal.h"

#include <Windows.h>

#define dynlib_max_symbol_name 128

static ThreadMutex g_dynlibLoadMutex;

void dynlib_pal_init(void) {
  /**
   * Disable Windows ui error popups that could be shown as a result of calling 'LoadLibrary'.
   */
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX | SEM_NOGPFAULTERRORBOX);

  g_dynlibLoadMutex = thread_mutex_create(g_allocHeap);
}

void dynlib_pal_teardown(void) { thread_mutex_destroy(g_dynlibLoadMutex); }

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

DynLibResult dynlib_pal_load(Allocator* alloc, const String name, DynLib** out) {
  // Convert the path to a null-terminated wide-char string.
  const usize pathBufferSize = winutils_to_widestr_size(name);
  if (pathBufferSize >= MAX_PATH) {
    return DynLibResult_LibraryNameTooLong;
  }
  Mem pathBufferMem = mem_stack(pathBufferSize);
  winutils_to_widestr(pathBufferMem, name);

  HMODULE handle = LoadLibraryEx(pathBufferMem.ptr, null, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
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

void dynlib_pal_destroy(DynLib* lib) {
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

String dynlib_pal_path(const DynLib* lib) { return lib->path; }

Symbol dynlib_pal_symbol(const DynLib* lib, const String name) {
  // Copy the name on the stack and null-terminate it.
  if (name.size >= dynlib_max_symbol_name) {
    diag_crash_msg("Symbol name too long");
  }
  Mem nameBuffer = mem_stack(dynlib_max_symbol_name);
  mem_cpy(nameBuffer, name);
  *mem_at_u8(nameBuffer, name.size) = '\0';

  return (Symbol)GetProcAddress(lib->handle, nameBuffer.ptr);
}
