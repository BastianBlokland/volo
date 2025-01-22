#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_thread.h"
#include "core_winutils.h"

#include "dynlib_internal.h"

#include <Windows.h>
#include <psapi.h>

#define dynlib_max_symbol_name 128
#define dynlib_debug false

/**
 * Win32 dynamic library loading.
 *
 * NOTE: This emulates the unix dlopen / dlsym behavior where dlsym can also find symbols in the
 * dependencies of a library. On Windows this is awkward to implement and requires tracking
 * dependencies using EnumProcessModules before and after a LoadLibrary, but it makes the api usage
 * much nicer.
 */

typedef struct {
  HMODULE handle;
  HMODULE parent;
  u64     sequence; // Module is currently loaded if sequence equals 'g_dynlibInfoSequence'.
} LibInfo;

static ThreadMutex g_dynlibLoadMutex;
static HMODULE     g_dynlibRootModule;
static u64         g_dynlibInfoSequence;
static DynArray    g_dynlibInfo; // LibInfo[], sorted on 'handle'.

void dynlib_pal_init(void) {
  g_dynlibLoadMutex  = thread_mutex_create(g_allocPersist);
  g_dynlibRootModule = GetModuleHandle(null);
  g_dynlibInfo       = dynarray_create_t(g_allocPersist, LibInfo, 64);

  /**
   * Disable Windows ui error popups that could be shown as a result of calling 'LoadLibrary'.
   */
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX | SEM_NOGPFAULTERRORBOX);
}

void dynlib_pal_teardown(void) {
  dynarray_destroy(&g_dynlibInfo);
  thread_mutex_destroy(g_dynlibLoadMutex);
}

struct sDynLib {
  HMODULE    handle;
  String     path;
  Allocator* alloc;
};

static String dynlib_module_path_scratch(HMODULE module) {
  Mem widePathBuffer       = mem_stack((MAX_PATH + 1) * sizeof(wchar_t)); // +1 for null-terminator.
  const usize widePathSize = GetModuleFileName(module, widePathBuffer.ptr, MAX_PATH);
  if (!widePathSize) {
    const DWORD err = GetLastError();
    diag_crash_msg(
        "GetModuleFileName() failed: {}, {}",
        fmt_int((u64)err),
        fmt_text(winutils_error_msg_scratch(err)));
  }
  return winutils_from_widestr_scratch(widePathBuffer.ptr, widePathSize);
}

static bool dynlib_info_loaded(const LibInfo* libInfo) {
  return libInfo->sequence == g_dynlibInfoSequence;
}

static i8 dynlib_info_compare(const void* a, const void* b) {
  return compare_uptr(field_ptr(a, LibInfo, handle), field_ptr(b, LibInfo, handle));
}

static void dynlib_info_update(HANDLE parent) {
  HANDLE  process = GetCurrentProcess();
  HMODULE modules[1024];

  DWORD neededBytes;
  if (!K32EnumProcessModules(process, modules, sizeof(modules), &neededBytes)) {
    const unsigned long errCode = GetLastError();
    const String        errMsg  = winutils_error_msg_scratch(errCode);
    diag_crash_msg("EnumProcessModules() failed: {} {}", fmt_int((u64)errCode), fmt_text(errMsg));
  }
  MAYBE_UNUSED const u64 prevSequence = g_dynlibInfoSequence++; // Invalidate all modules.

  const u32 moduleCount = neededBytes / sizeof(HMODULE);
  for (u32 i = 0; i != moduleCount; ++i) {
    LibInfo* info = dynarray_find_or_insert_sorted(&g_dynlibInfo, dynlib_info_compare, &modules[i]);
    if (info->handle) {
      // Module was already loaded; update its sequence number to track that its still loaded.
      info->sequence = g_dynlibInfoSequence;
    } else {
      info->handle   = modules[i];
      info->parent   = parent;
      info->sequence = g_dynlibInfoSequence;

#if dynlib_debug
      diag_print("DynLib: Loaded module: {}\n", fmt_text(dynlib_module_path_scratch(modules[i])));
#endif
    }
  }

#if dynlib_debug
  dynarray_for_t(&g_dynlibInfo, LibInfo, libInfo) {
    if (libInfo->sequence == prevSequence) {
      diag_print_raw(string_lit("DynLib: Unloaded module\n"));
    }
  }
#endif
}

DynLibResult dynlib_pal_load(Allocator* alloc, const String name, DynLib** out) {
  // Convert the path to a null-terminated wide-char string.
  const usize pathBufferSize = winutils_to_widestr_size(name);
  if (pathBufferSize >= MAX_PATH) {
    return DynLibResult_LibraryNameTooLong;
  }
  Mem pathBufferMem = mem_stack(pathBufferSize);
  winutils_to_widestr(pathBufferMem, name);

  HMODULE handle;
  thread_mutex_lock(g_dynlibLoadMutex);
  {
    dynlib_info_update(g_dynlibRootModule); // Attribute any externally loaded modules to the root.
    handle = LoadLibraryEx(pathBufferMem.ptr, null, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (handle) {
      dynlib_info_update(handle); // Attribute any newly loaded modules to the new handle.
    }
  }
  thread_mutex_unlock(g_dynlibLoadMutex);

  if (!handle) {
    return DynLibResult_LibraryNotFound;
  }

  *out  = alloc_alloc_t(alloc, DynLib);
  **out = (DynLib){
      .handle = handle,
      .path   = alloc_dup(alloc, dynlib_module_path_scratch(handle), 1),
      .alloc  = alloc,
  };
  return DynLibResult_Success;
}

void dynlib_pal_destroy(DynLib* lib) {
  thread_mutex_lock(g_dynlibLoadMutex);
  {
    if (UNLIKELY(FreeLibrary(lib->handle) == 0)) {
      const DWORD err = GetLastError();
      diag_crash_msg(
          "FreeLibrary() failed: {}, {}",
          fmt_int((u64)err),
          fmt_text(winutils_error_msg_scratch(err)));
    }
    dynlib_info_update(g_dynlibRootModule); // Attribute any externally loaded modules to the root.
  }
  thread_mutex_unlock(g_dynlibLoadMutex);

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

  Symbol res = (Symbol)GetProcAddress(lib->handle, nameBuffer.ptr);
  if (res) {
    return res; // Symbol was found in the module itself.
  }

  // Attempt to find the symbol in any of the child modules.
  thread_mutex_lock(g_dynlibLoadMutex);
  {
    dynarray_for_t(&g_dynlibInfo, LibInfo, libInfo) {
      if (!dynlib_info_loaded(libInfo)) {
        continue; // Module has been unloaded.
      }
      if (libInfo->parent != lib->handle) {
        continue; // Not a child of the specified module.
      }
      if (libInfo->handle == lib->handle) {
        continue; // We've already checked the module itself.
      }
      res = (Symbol)GetProcAddress(libInfo->handle, nameBuffer.ptr);
      if (res) {
        break; // Symbol found.
      }
    }
  }
  thread_mutex_unlock(g_dynlibLoadMutex);
  return res;
}
