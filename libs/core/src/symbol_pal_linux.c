#include "core_alloc.h"
#include "core_dynlib.h"
#include "core_thread.h"

#include "symbol_internal.h"

typedef enum {
  SymbolResolver_Ready,
  SymbolResolver_Error,
} SymbolResolverState;

typedef struct {
  Allocator*          alloc;
  SymbolResolverState state;

  DynLib* libDw;
  void*   dwarf_begin;
  void*   dwarf_addrdie;
  void*   dwarf_getscopes;
  void*   dwarf_diename;
  void*   dwarf_tag;
} SymbolResolver;

typedef struct {
  String name;
} SymbolInfo;

static SymbolResolver* g_symbolResolver;
static ThreadMutex     g_symbolResolverMutex;

static bool libdw_load(SymbolResolver* resolver) {
  DynLibResult loadRes = dynlib_load(resolver->alloc, string_lit("libdw.so"), &resolver->libDw);
  if (loadRes != DynLibResult_Success) {
    return false;
  }

#define DW_LOAD_SYM(_NAME_)                                                                        \
  do {                                                                                             \
    resolver->_NAME_ = dynlib_symbol(resolver->libDw, string_lit(#_NAME_));                        \
    if (!resolver->_NAME_) {                                                                       \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  DW_LOAD_SYM(dwarf_begin);
  DW_LOAD_SYM(dwarf_addrdie);
  DW_LOAD_SYM(dwarf_getscopes);
  DW_LOAD_SYM(dwarf_diename);
  DW_LOAD_SYM(dwarf_tag);

  return true;
}

static SymbolResolver* symbol_resolver_create(Allocator* alloc) {
  SymbolResolver* resolver = alloc_alloc_t(alloc, SymbolResolver);
  *resolver                = (SymbolResolver){.alloc = alloc};

  if (libdw_load(resolver)) {
    resolver->state = SymbolResolver_Ready;
  } else {
    resolver->state = SymbolResolver_Error;
  }
  return resolver;
}

static void symbol_resolver_destroy(SymbolResolver* resolver) {
  if (resolver->libDw) {
    dynlib_destroy(resolver->libDw);
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
