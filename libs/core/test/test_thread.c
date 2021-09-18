#include "check_spec.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"

static void test_thread_has_name(void* data) {
  (void)data;
  if (!string_eq(g_thread_name, string_lit("my_custom_name"))) {
    diag_crash_msg("Test 'test_thread_has_name' failed");
  }
}

static void test_atomic_store_value(void* data) { thread_atomic_store_i64((i64*)data, 1337); }

static void test_atomic_exchange_value(void* data) {
  if (thread_atomic_exchange_i64((i64*)data, 1337) != 42) {
    diag_crash_msg("Test 'test_atomic_exchange_value' failed");
  }
}

static void test_atomic_compare_exchange_value(void* data) {
  i64* valPtr = (i64*)data;
  for (i32 i = 0; i != 1000; ++i) {
    i64 expected = 42;
    if (!thread_atomic_compare_exchange_i64(valPtr, &expected, 1337)) {
      if (expected != 1337) {
        diag_crash_msg("Test 'test_atomic_compare_exchange_value' failed");
      }
    }
  }
}

static void test_atomic_add_value(void* data) {
  i64* valPtr = (i64*)data;
  for (i32 i = 0; i != 10000; ++i) {
    thread_atomic_add_i64(valPtr, 1);
  }
}

static void test_atomic_sub_value(void* data) {
  i64* valPtr = (i64*)data;
  for (i32 i = 0; i != 10000; ++i) {
    thread_atomic_sub_i64(valPtr, 1);
  }
}

static void test_mutex_trylock_fails(void* data) {
  ThreadMutex mutex = (ThreadMutex)data;
  if (thread_mutex_trylock(mutex)) {
    diag_crash_msg("Test 'test_mutex_trylock_fails' failed");
  }
}

static void test_cond_broadcast_unblocks_all(void* rawData) {
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

static void test_cond_signal_unblocks_atleast_one(void* rawData) {
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

spec(thread) {

  it("assigns names to threads") {
    ThreadHandle exec = thread_start(test_thread_has_name, null, string_lit("my_custom_name"));
    thread_join(exec);
  }

  it("can store and load integers atomically") {
    i64          value = 0;
    ThreadHandle exec  = thread_start(test_atomic_store_value, &value, string_lit("volo_test"));
    thread_join(exec);
    check_eq_int(thread_atomic_load_i64(&value), 1337);
  }

  it("can exchange integers atomically") {
    i64          value = 42;
    ThreadHandle exec  = thread_start(test_atomic_exchange_value, &value, string_lit("volo_test"));
    thread_join(exec);
    check_eq_int(thread_atomic_load_i64(&value), 1337);
  }

  it("can compare and exchange integers atomically") {
    i64          value = 42;
    ThreadHandle exec =
        thread_start(test_atomic_compare_exchange_value, &value, string_lit("volo_test"));
    for (i32 i = 0; i != 1000; ++i) {
      i64 expected = 1337;
      if (!thread_atomic_compare_exchange_i64(&value, &expected, 42)) {
        check_eq_int(expected, 42);
      }
    }
    thread_join(exec);
    check(thread_atomic_load_i64(&value) == 1337 || thread_atomic_load_i64(&value) == 42);
  }

  it("can add integers atomically") {
    i64          value = 0;
    ThreadHandle exec  = thread_start(test_atomic_add_value, &value, string_lit("volo_test"));
    for (i32 i = 0; i != 10000; ++i) {
      thread_atomic_add_i64(&value, 1);
    }
    thread_join(exec);
    check_eq_int(thread_atomic_load_i64(&value), 20000);
  }

  it("can substract integers atomically") {
    i64          value = 20000;
    ThreadHandle exec  = thread_start(test_atomic_sub_value, &value, string_lit("volo_test"));
    for (i32 i = 0; i != 10000; ++i) {
      thread_atomic_sub_i64(&value, 1);
    }
    thread_join(exec);
    check_eq_int(thread_atomic_load_i64(&value), 0);
  }

  it("can lock a mutex when its currently unlocked") {
    ThreadMutex mutex = thread_mutex_create(g_alloc_scratch);

    thread_mutex_lock(mutex);
    thread_mutex_unlock(mutex);

    thread_mutex_destroy(mutex);
  }

  it("can trylock a mutex when its currently unlocked") {
    ThreadMutex mutex = thread_mutex_create(g_alloc_scratch);

    check(thread_mutex_trylock(mutex));
    thread_mutex_unlock(mutex);

    thread_mutex_destroy(mutex);
  }

  it("fails to trylock when a mutex is currently locked") {
    ThreadMutex mutex = thread_mutex_create(g_alloc_scratch);

    thread_mutex_lock(mutex);

    ThreadHandle exec =
        thread_start(test_mutex_trylock_fails, (void*)mutex, string_lit("volo_test"));
    thread_join(exec);

    thread_mutex_unlock(mutex);
    thread_mutex_destroy(mutex);
  }

  it("unlocks atleast one waiter when signaling a condition") {
    struct {
      bool            started;
      i64             value;
      ThreadCondition cond;
      ThreadMutex     mutex;
    } data;

    data.started = false;
    data.value   = 0;
    data.mutex   = thread_mutex_create(g_alloc_scratch);
    data.cond    = thread_cond_create(g_alloc_scratch);

    ThreadHandle exec =
        thread_start(test_cond_signal_unblocks_atleast_one, &data, string_lit("volo_test"));

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

  it("unblocks all waiters when broadcasting a condition") {
    struct {
      u16             startedExecs;
      ThreadCondition cond;
      ThreadMutex     mutex;
    } data;

    data.startedExecs = 0;
    data.mutex        = thread_mutex_create(g_alloc_scratch);
    data.cond         = thread_cond_create(g_alloc_scratch);

    ThreadHandle threads[4];
    for (usize i = 0; i != array_elems(threads); ++i) {
      threads[i] = thread_start(
          test_cond_broadcast_unblocks_all, &data, fmt_write_scratch("volo_test_{}", fmt_int(i)));
    }

    while (data.startedExecs < array_elems(threads)) {
      thread_yield();
    }

    thread_mutex_lock(data.mutex);
    thread_cond_broadcast(data.cond);
    thread_mutex_unlock(data.mutex);

    for (usize i = 0; i != array_elems(threads); ++i) {
      thread_join(threads[i]);
    }
    thread_mutex_destroy(data.mutex);
    thread_cond_destroy(data.cond);
  }
}
