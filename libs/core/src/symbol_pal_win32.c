#include "core_alloc.h"
#include "core_thread.h"

#include "symbol_internal.h"

static ThreadMutex g_symResolverMutex;

void symbol_pal_init(void) { g_symResolverMutex = thread_mutex_create(g_alloc_persist); }

void symbol_pal_teardown(void) { thread_mutex_destroy(g_symResolverMutex); }

String symbol_pal_name(Symbol symbol) {
  String result = string_empty;
  thread_mutex_lock(g_symResolverMutex);
  {
    // TODO: Implement symbol resolution.
  }
  thread_mutex_unlock(g_symResolverMutex);
  return result;
}
