#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"

#include "alloc_internal.h"
#include "dynlib_internal.h"

static bool g_dynlibInitialized;
static i64  g_dynlibCount;

void dynlib_init(void) {
  dynlib_pal_init();
  g_dynlibInitialized = true;
}

void dynlib_leak_detect(void) {
  if (UNLIKELY(thread_atomic_load_i64(&g_dynlibCount) != 0)) {
    alloc_crash_with_msg("dynlib: {} libary(s) leaked", fmt_int(g_dynlibCount));
  }
}

static const String g_dynlibResultStrs[] = {
    string_static("DynLibSuccess"),
    string_static("DynLibLibraryNameTooLong"),
    string_static("DynLibLibraryNotFound"),
    string_static("DynLibUnknownError"),
};

ASSERT(array_elems(g_dynlibResultStrs) == DynLibResult_Count, "Incorrect number of result names");

String dynlib_result_str(const DynLibResult result) {
  diag_assert(result < DynLibResult_Count);
  return g_dynlibResultStrs[result];
}

DynLibResult dynlib_load(Allocator* alloc, const String name, DynLib** out) {
  if (UNLIKELY(!g_dynlibInitialized)) {
    diag_crash_msg("dynlib: Not initialized");
  }
  const DynLibResult res = dynlib_pal_load(alloc, name, out);
  if (res == DynLibResult_Success) {
    thread_atomic_add_i64(&g_dynlibCount, 1);
  }
  return res;
}

DynLibResult
dynlib_load_first(Allocator* alloc, const String names[], const u32 nameCount, DynLib** out) {
  if (UNLIKELY(!g_dynlibInitialized)) {
    diag_crash_msg("dynlib: Not initialized");
  }
  for (u32 i = 0; i != nameCount; ++i) {
    const DynLibResult res = dynlib_pal_load(alloc, names[i], out);
    switch (res) {
    case DynLibResult_Success:
      thread_atomic_add_i64(&g_dynlibCount, 1);
      return DynLibResult_Success;
    case DynLibResult_LibraryNotFound:
      continue; // Try the next name.
    default:
      return res; // Library failed to load; return error.
    }
  }
  return DynLibResult_LibraryNotFound;
}

void dynlib_destroy(DynLib* lib) {
  dynlib_pal_destroy(lib);
  if (UNLIKELY(thread_atomic_sub_i64(&g_dynlibCount, 1) <= 0)) {
    diag_crash_msg("dynlib: Double destroy of dynlib");
  }
}

String dynlib_path(const DynLib* lib) { return dynlib_pal_path(lib); }

Symbol dynlib_symbol(const DynLib* lib, const String name) { return dynlib_pal_symbol(lib, name); }

u32 dynlib_count(void) { return (u32)thread_atomic_load_i64(&g_dynlibCount); }
