#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_file.h"
#include "core_path.h"
#include "core_thread.h"

#include "file_internal.h"
#include "symbol_internal.h"

#define dwarf_cmd_read 0

typedef struct sDwarf       Dwarf;
typedef struct sDwarfCu     DwarfCu;
typedef struct sDwarfAbbrev DwarfAbbrev;

typedef struct {
  void*        addr;
  DwarfCu*     cu;
  DwarfAbbrev* abbrev;
  long int     padding;
} DwarfDie;

typedef enum {
  SymbolResolver_Init,
  SymbolResolver_Ready,
  SymbolResolver_Error,
} SymbolResolverState;

typedef struct {
  Allocator*          alloc;
  SymbolResolverState state;
  File*               exec; // Handle to our own executable.

  DynLib* dwLib;
  Dwarf*  dwSession;

  // clang-format off
  Dwarf*      (SYS_DECL* dwarf_begin)(int fildes, int cmd);
  int         (SYS_DECL* dwarf_end)(Dwarf*);
  DwarfDie*   (SYS_DECL* dwarf_addrdie)(Dwarf*, uptr addr, DwarfDie* result);
  int         (SYS_DECL* dwarf_getscopes)(DwarfDie* cuDie, uptr addr, DwarfDie** scopes);
  const char* (SYS_DECL* dwarf_diename)(DwarfDie*);
  int         (SYS_DECL* dwarf_tag)(DwarfDie*);
  // clang-format on
} SymbolResolver;

typedef struct {
  String name;
} SymbolInfo;

static SymbolResolver* g_symbolResolver;
static ThreadMutex     g_symbolResolverMutex;

static bool dw_load(SymbolResolver* resolver) {
  DynLibResult loadRes = dynlib_load(resolver->alloc, string_lit("libdw.so.1"), &resolver->dwLib);
  if (loadRes != DynLibResult_Success) {
    return false;
  }

#define DW_LOAD_SYM(_NAME_)                                                                        \
  do {                                                                                             \
    resolver->_NAME_ = dynlib_symbol(resolver->dwLib, string_lit(#_NAME_));                        \
    if (!resolver->_NAME_) {                                                                       \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  DW_LOAD_SYM(dwarf_begin);
  DW_LOAD_SYM(dwarf_end);
  DW_LOAD_SYM(dwarf_addrdie);
  DW_LOAD_SYM(dwarf_getscopes);
  DW_LOAD_SYM(dwarf_diename);
  DW_LOAD_SYM(dwarf_tag);

  return true;
}

static bool dw_begin(SymbolResolver* resolver) {
  diag_assert(resolver->state == SymbolResolver_Init);
  diag_assert(!resolver->dwSession);
  resolver->dwSession = resolver->dwarf_begin(resolver->exec->handle, dwarf_cmd_read);
  return resolver->dwSession != null;
}

static void dw_end(SymbolResolver* resolver) {
  diag_assert(resolver->dwSession);
  resolver->dwarf_end(resolver->dwSession);
}

static SymbolResolver* symbol_resolver_create(Allocator* alloc) {
  SymbolResolver* resolver = alloc_alloc_t(alloc, SymbolResolver);
  *resolver                = (SymbolResolver){.alloc = alloc};

  if (file_create(alloc, g_path_executable, FileMode_Open, FileAccess_Read, &resolver->exec)) {
    goto Error;
  }
  if (!dw_load(resolver)) {
    goto Error;
  }
  if (!dw_begin(resolver)) {
    goto Error;
  }
  resolver->state = SymbolResolver_Ready;
  return resolver;

Error:
  resolver->state = SymbolResolver_Error;
  return resolver;
}

static void symbol_resolver_destroy(SymbolResolver* resolver) {
  if (resolver->dwSession) {
    dw_end(resolver);
  }
  if (resolver->exec) {
    file_destroy(resolver->exec);
  }
  if (resolver->dwLib) {
    dynlib_destroy(resolver->dwLib);
  }
  alloc_free_t(resolver->alloc, resolver);
}

static bool symbol_resolver_lookup(SymbolResolver* resolver, Symbol symbol, SymbolInfo* out) {
  if (resolver->state != SymbolResolver_Ready) {
    return false;
  }
  (void)resolver;
  (void)symbol;
  (void)out;
  return false;
}

void symbol_pal_init(void) { g_symbolResolverMutex = thread_mutex_create(g_alloc_persist); }

void symbol_pal_teardown(void) {
  if (g_symbolResolver) {
    symbol_resolver_destroy(g_symbolResolver);
  }
  thread_mutex_destroy(g_symbolResolverMutex);
}

String symbol_pal_name(Symbol symbol) {
  String result = string_empty;
  thread_mutex_lock(g_symbolResolverMutex);
  {
    if (!g_symbolResolver) {
      g_symbolResolver = symbol_resolver_create(g_alloc_heap);
    }

    SymbolInfo info;
    if (symbol_resolver_lookup(g_symbolResolver, symbol, &info)) {
      result = info.name;
    }
  }
  thread_mutex_unlock(g_symbolResolverMutex);
  return result;
}
