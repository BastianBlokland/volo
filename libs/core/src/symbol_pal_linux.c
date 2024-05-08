#include "core_alloc.h"
#include "core_thread.h"

#include "symbol_internal.h"

typedef struct {
  i32 dummy;
} SymbolResolver;

typedef struct {
  String name;
} SymbolInfo;

static SymbolResolver* g_symbolResolver;
static ThreadMutex     g_symbolResolverMutex;

static SymbolResolver* symbol_resolver_create(Allocator* alloc) {
  SymbolResolver* resolver = alloc_alloc_t(alloc, SymbolResolver);

  *resolver = (SymbolResolver){
      .dummy = 0,
  };

  return resolver;
}

static void symbol_resolver_destroy(SymbolResolver* resolver, Allocator* alloc) {
  alloc_free_t(alloc, resolver);
}

static bool symbol_resolver_lookup(SymbolResolver* resolver, Symbol symbol, SymbolInfo* out) {
  (void)resolver;
  (void)symbol;
  (void)out;
  return false;
}

void symbol_pal_init(void) { g_symbolResolverMutex = thread_mutex_create(g_alloc_persist); }

void symbol_pal_teardown(void) {
  if (g_symbolResolver) {
    symbol_resolver_destroy(g_symbolResolver, g_alloc_persist);
  }
  thread_mutex_destroy(g_symbolResolverMutex);
}

String symbol_pal_name(Symbol symbol) {
  String result = string_empty;
  thread_mutex_lock(g_symbolResolverMutex);
  {
    if (!g_symbolResolver) {
      g_symbolResolver = symbol_resolver_create(g_alloc_persist);
    }

    SymbolInfo info;
    if (symbol_resolver_lookup(g_symbolResolver, symbol, &info)) {
      result = info.name;
    }
  }
  thread_mutex_unlock(g_symbolResolverMutex);
  return result;
}
