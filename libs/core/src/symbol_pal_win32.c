#include "core_alloc.h"
#include "core_thread.h"

#include "symbol_internal.h"

typedef struct {
  Allocator* alloc;
} SymResolver;

static SymResolver* g_symResolver;
static ThreadMutex  g_symResolverMutex;

static SymResolver* resolver_create(Allocator* alloc) {
  SymResolver* r = alloc_alloc_t(alloc, SymResolver);
  *r             = (SymResolver){.alloc = alloc};
  return r;
}

static void resolver_destroy(SymResolver* r) { alloc_free_t(r->alloc, r); }

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
    // TODO: Implement symbol resolution.
  }
  thread_mutex_unlock(g_symResolverMutex);
  return result;
}
