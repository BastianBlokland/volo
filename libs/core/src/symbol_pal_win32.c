#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_path.h"
#include "core_thread.h"

#include "symbol_internal.h"

#include <Windows.h>

typedef struct {
  uptr   addr;
  String name;
} SymInfo;

typedef enum {
  SymResolver_Init,
  SymResolver_Ready,
  SymResolver_Error,
} SymResolverState;

typedef struct {
  Allocator*       alloc;
  SymResolverState state;
  HANDLE           process;

  DynLib* dbgHelp;
  bool    dbgHelpActive;
  u64     dbgHelpBaseAddr;

  // clang-format off
  BOOL    (SYS_DECL* SymInitialize)(HANDLE process, PCSTR userSearchPath, BOOL invadeProcess);
  BOOL    (SYS_DECL* SymCleanup)(HANDLE process);
  DWORD64 (SYS_DECL* SymLoadModuleEx)(HANDLE process, HANDLE file, PCSTR imageName, PCSTR moduleName, DWORD64 baseOfDll, DWORD dllSize, void* data, DWORD flags);
  // clang-format on
} SymResolver;

static SymResolver* g_symResolver;
static ThreadMutex  g_symResolverMutex;

static const char* to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_alloc_scratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

static bool resolver_dbghelp_load(SymResolver* r) {
  DynLibResult loadRes = dynlib_load(r->alloc, string_lit("Dbghelp.dll"), &r->dbgHelp);
  if (loadRes != DynLibResult_Success) {
    return false;
  }

#define DBG_LOAD_SYM(_NAME_)                                                                       \
  do {                                                                                             \
    r->_NAME_ = dynlib_symbol(r->dbgHelp, string_lit(#_NAME_));                                    \
    if (!r->_NAME_) {                                                                              \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  DBG_LOAD_SYM(SymInitialize);
  DBG_LOAD_SYM(SymCleanup);
  DBG_LOAD_SYM(SymLoadModuleEx);

  return true;
}

/**
 * Debug info search path.
 * NOTE: We only include the executable's own directory.
 */
static const char* resolver_dbghelp_searchpath() {
  const String execParentPath = path_parent(g_path_executable);
  return to_null_term_scratch(execParentPath);
}

static bool resolver_dbghelp_begin(SymResolver* r) {
  diag_assert(r->state == SymResolver_Init);
  const BOOL invadeProcess = false; // Do not automatically load dbg-info for all modules.
  if (!r->SymInitialize(r->process, resolver_dbghelp_searchpath(), invadeProcess)) {
    return false;
  }
  const char* imageName = to_null_term_scratch(g_path_executable);
  r->dbgHelpActive      = true;
  r->dbgHelpBaseAddr    = r->SymLoadModuleEx(r->process, null, imageName, null, 0, 0, null, 0);
  return r->dbgHelpBaseAddr != 0;
}

static void resolver_dbghelp_end(SymResolver* r) { r->SymCleanup(r->process); }

static SymResolver* resolver_create(Allocator* alloc) {
  SymResolver* r = alloc_alloc_t(alloc, SymResolver);
  *r             = (SymResolver){
                  .alloc   = alloc,
                  .process = GetCurrentProcess(),
  };

  if (!resolver_dbghelp_load(r)) {
    goto Error;
  }
  if (!resolver_dbghelp_begin(r)) {
    goto Error;
  }
  r->state = SymResolver_Ready;
  return r;

Error:
  r->state = SymResolver_Error;
  return r;
}

static void resolver_destroy(SymResolver* r) {
  if (r->dbgHelpActive) {
    resolver_dbghelp_end(r);
  }
  if (r->dbgHelp) {
    dynlib_destroy(r->dbgHelp);
  }
  alloc_free_t(r->alloc, r);
}

static const SymInfo* resolver_lookup(SymResolver* r, Symbol symbol) {
  if (r->state != SymResolver_Ready) {
    return null;
  }
  // TODO: Implement symbol resolution.
  static SymInfo g_dummySymbol;
  g_dummySymbol.name = string_lit("dummy");
  return &g_dummySymbol;
}

void symbol_pal_init(void) { g_symResolverMutex = thread_mutex_create(g_alloc_persist); }

void symbol_pal_teardown(void) {
  if (g_symResolver) {
    resolver_destroy(g_symResolver);
  }
  thread_mutex_destroy(g_symResolverMutex);
}

String symbol_pal_name(Symbol symbol) {
  String result = string_empty;
  thread_mutex_lock(g_symResolverMutex);
  {
    if (!g_symResolver) {
      g_symResolver = resolver_create(g_alloc_heap);
    }
    const SymInfo* info = resolver_lookup(g_symResolver, symbol);
    if (info) {
      result = info->name;
    }
  }
  thread_mutex_unlock(g_symResolverMutex);
  return result;
}
