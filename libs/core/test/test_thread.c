#include "core_diag.h"
#include "core_thread.h"

static void test_thread_has_name() {
  diag_assert(string_eq(g_thread_name, string_lit("volo_main")));
}

static void test_thread_new_thread_has_different_tid_exec(void* data) {
  (void)data;
  diag_assert(g_thread_tid != g_thread_main_tid);
}

static void test_thread_new_thread_has_different_tid() {
  ThreadHandle exec = thread_start(
      test_thread_new_thread_has_different_tid_exec, null, string_lit("volo_test_exec"));
  thread_join(exec);
}

static void test_thread_new_thread_has_name_exec(void* data) {
  (void)data;
  diag_assert(string_eq(g_thread_name, string_lit("my_custom_name")));
}

static void test_thread_new_thread_has_name() {
  ThreadHandle exec =
      thread_start(test_thread_new_thread_has_name_exec, null, string_lit("my_custom_name"));
  thread_join(exec);
}

// static void test_thread_store_value_exec(void* data) {
//   atomic_i32* result = (atomic_i32*)data;
//   atomic_store_explicit(result, 1337, memory_order_release);
// }

// static void test_thread_store_value() {
//   atomic_i32   value = 42;
//   ThreadHandle exec =
//       thread_start(test_thread_store_value_exec, &value, string_lit("volo_test_exec"));
//   thread_join(exec);
//   diag_assert(atomic_load_explicit(&value, memory_order_acquire) == 1337);
// }

static void test_thread_mutex_lock_succeeds_when_unlocked() {
  ThreadMutex mutex = thread_mutex_create(g_alloc_heap);

  thread_mutex_lock(mutex);
  thread_mutex_unlock(mutex);

  thread_mutex_destroy(mutex);
}

static void test_thread_mutex_trylock_succeeds_when_unlocked() {
  ThreadMutex mutex = thread_mutex_create(g_alloc_heap);

  diag_assert(thread_mutex_trylock(mutex));
  thread_mutex_unlock(mutex);

  thread_mutex_destroy(mutex);
}

static void test_thread_mutex_trylock_fail_when_when_locked_exec(void* data) {
  ThreadMutex mutex = (ThreadMutex)data;
  diag_assert(!thread_mutex_trylock(mutex));
}

static void test_thread_mutex_trylock_fails_when_locked() {
  ThreadMutex mutex = thread_mutex_create(g_alloc_heap);

  thread_mutex_lock(mutex);

  ThreadHandle exec = thread_start(
      test_thread_mutex_trylock_fail_when_when_locked_exec,
      (void*)mutex,
      string_lit("volo_test_exec"));
  thread_join(exec);

  thread_mutex_unlock(mutex);
  thread_mutex_destroy(mutex);
}

void test_thread() {

  diag_assert(g_thread_tid == g_thread_main_tid);

  test_thread_has_name();
  test_thread_new_thread_has_different_tid();
  test_thread_new_thread_has_name();
  // test_thread_store_value();
  test_thread_mutex_lock_succeeds_when_unlocked();
  test_thread_mutex_trylock_succeeds_when_unlocked();
  test_thread_mutex_trylock_fails_when_locked();
}
