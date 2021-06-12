#include "core_array.h"
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

static void test_thread_atomic_store_value_exec(void* data) {
  thread_atomic_store_i64((i64*)data, 1337);
}

static void test_thread_atomic_store_value() {
  i64          value = 0;
  ThreadHandle exec =
      thread_start(test_thread_atomic_store_value_exec, &value, string_lit("volo_test_exec"));
  thread_join(exec);
  diag_assert(thread_atomic_load_i64(&value) == 1337);
}

static void test_thread_atomic_exchange_value_exec(void* data) {
  diag_assert(thread_atomic_exchange_i64((i64*)data, 1337) == 42);
}

static void test_thread_atomic_exchange_value() {
  i64          value = 42;
  ThreadHandle exec =
      thread_start(test_thread_atomic_exchange_value_exec, &value, string_lit("volo_test_exec"));
  thread_join(exec);
  diag_assert(thread_atomic_load_i64(&value) == 1337);
}

static void test_thread_atomic_compare_exchange_value_exec(void* data) {
  i64* valPtr = (i64*)data;
  for (i32 i = 0; i != 1000; ++i) {
    i64 expected = 42;
    if (!thread_atomic_compare_exchange_i64(valPtr, &expected, 1337)) {
      diag_assert(expected == 1337);
    }
  }
}

static void test_thread_atomic_compare_exchange_value() {
  i64          value = 42;
  ThreadHandle exec  = thread_start(
      test_thread_atomic_compare_exchange_value_exec, &value, string_lit("volo_test_exec"));
  for (i32 i = 0; i != 1000; ++i) {
    i64 expected = 1337;
    if (!thread_atomic_compare_exchange_i64(&value, &expected, 42)) {
      diag_assert(expected == 42);
    }
  }
  thread_join(exec);
  diag_assert(thread_atomic_load_i64(&value) == 1337 || thread_atomic_load_i64(&value) == 42);
}

static void test_thread_atomic_add_value_exec(void* data) {
  i64* valPtr = (i64*)data;
  for (i32 i = 0; i != 10000; ++i) {
    thread_atomic_add_i64(valPtr, 1);
  }
}

static void test_thread_atomic_add_value() {
  i64          value = 0;
  ThreadHandle exec =
      thread_start(test_thread_atomic_add_value_exec, &value, string_lit("volo_test_exec"));
  for (i32 i = 0; i != 10000; ++i) {
    thread_atomic_add_i64(&value, 1);
  }
  thread_join(exec);
  diag_assert(thread_atomic_load_i64(&value) == 20000);
}

static void test_thread_atomic_sub_value_exec(void* data) {
  i64* valPtr = (i64*)data;
  for (i32 i = 0; i != 10000; ++i) {
    thread_atomic_sub_i64(valPtr, 1);
  }
}

static void test_thread_atomic_sub_value() {
  i64          value = 20000;
  ThreadHandle exec =
      thread_start(test_thread_atomic_sub_value_exec, &value, string_lit("volo_test_exec"));
  for (i32 i = 0; i != 10000; ++i) {
    thread_atomic_sub_i64(&value, 1);
  }
  thread_join(exec);
  diag_assert(thread_atomic_load_i64(&value) == 0);
}

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

static void test_thread_cond_signal_unblocks_atleast_one_exec(void* rawData) {
  struct {
    bool            started;
    i64             value;
    ThreadCondition cond;
    ThreadMutex     mutex;
  }* data = rawData;

  thread_mutex_lock(data->mutex);
  data->started = true;
  while (data->value != 1337) {
    thread_cond_wait(data->cond, data->mutex);
  }
  data->value = 42;
  thread_mutex_unlock(data->mutex);
}

static void test_thread_cond_signal_unblocks_atleast_one() {
  struct {
    bool            started;
    i64             value;
    ThreadCondition cond;
    ThreadMutex     mutex;
  } data;

  data.started = false;
  data.value   = 0;
  data.mutex   = thread_mutex_create(g_alloc_heap);
  data.cond    = thread_cond_create(g_alloc_heap);

  ThreadHandle exec = thread_start(
      test_thread_cond_signal_unblocks_atleast_one_exec, &data, string_lit("volo_test_exec"));

  while (!data.started) {
    thread_yield();
  }

  thread_mutex_lock(data.mutex);
  data.value = 1337;
  thread_cond_signal(data.cond);
  thread_mutex_unlock(data.mutex);

  thread_join(exec);
  thread_mutex_destroy(data.mutex);
  thread_cond_destroy(data.cond);
}

static void test_thread_cond_broadcast_unblocks_all_exec(void* rawData) {
  struct {
    u16             startedExecs;
    ThreadCondition cond;
    ThreadMutex     mutex;
  }* data = rawData;

  thread_mutex_lock(data->mutex);
  ++data->startedExecs;
  thread_cond_wait(data->cond, data->mutex);
  thread_mutex_unlock(data->mutex);
}

static void test_thread_cond_broadcast_unblocks_all() {
  struct {
    u16             startedExecs;
    ThreadCondition cond;
    ThreadMutex     mutex;
  } data;

  data.startedExecs = 0;
  data.mutex        = thread_mutex_create(g_alloc_heap);
  data.cond         = thread_cond_create(g_alloc_heap);

  ThreadHandle threads[4];
  for (usize i = 0; i != array_elems(threads); ++i) {
    threads[i] = thread_start(
        test_thread_cond_broadcast_unblocks_all_exec, &data, string_lit("volo_test_exec"));
  }

  while (data.startedExecs < array_elems(threads)) {
    thread_yield();
  }

  thread_cond_broadcast(data.cond);

  for (usize i = 0; i != array_elems(threads); ++i) {
    thread_join(threads[i]);
  }
  thread_mutex_destroy(data.mutex);
  thread_cond_destroy(data.cond);
}

void test_thread() {

  diag_assert(g_thread_tid == g_thread_main_tid);

  test_thread_has_name();
  test_thread_new_thread_has_different_tid();
  test_thread_new_thread_has_name();
  test_thread_atomic_store_value();
  test_thread_atomic_exchange_value();
  test_thread_atomic_compare_exchange_value();
  test_thread_atomic_add_value();
  test_thread_atomic_sub_value();
  test_thread_mutex_lock_succeeds_when_unlocked();
  test_thread_mutex_trylock_succeeds_when_unlocked();
  test_thread_mutex_trylock_fails_when_locked();
  test_thread_cond_signal_unblocks_atleast_one();
  test_thread_cond_broadcast_unblocks_all();
}
