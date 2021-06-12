#pragma once
#include "core_alloc.h"
#include "core_annotation.h"
#include "core_string.h"
#include "core_time.h"
#include "core_types.h"

/**
 * Process identifier (aka 'thread group id').
 * The same value for all threads.
 */
extern i64 g_thread_pid;

/**
 * Thread identifier of the main thread.
 * Note: The thread that calls 'core_init()' is considered the main thread.
 */
extern i64 g_thread_main_tid;

/**
 * Thread identifier of the current thread.
 */
extern THREAD_LOCAL i64 g_thread_tid;

/**
 * Name of the current thread.
 */
extern THREAD_LOCAL String g_thread_name;

/**
 * Number of logical cpu cores available to this process.
 */
extern u16 g_thread_core_count;

/**
 * Function to run on an execution thread.
 */
typedef void (*ThreadRoutine)(void*);

/**
 * Handle to a started thread.
 * Note: Thread resources should be cleaned up by calling 'thread_join()'.
 */
typedef iptr ThreadHandle;

/**
 * Handle to a mutex.
 * Note: Should be cleaned up by calling 'thread_mutex_destroy()'.
 */
typedef iptr ThreadMutex;

/**
 * Atomically reads the value at the given pointer.
 * This includes a general memory barrier.
 */
i64 thread_atomic_load_i64(i64*);

/**
 * Atomically stores the value at the given pointer.
 * This includes a general memory barrier.
 */
void thread_atomic_store_i64(i64*, i64 value);

/**
 * Atomically stores the value at the given pointer and returns the old value.
 * This includes a general memory barrier.
 */
i64 thread_atomic_exchange_i64(i64*, i64 value);

/**
 * Compares the content of 'ptr' with the content of 'expected'. If equal the 'value' is stored into
 * 'ptr' and 'true' is returned. If not equal, the contents of 'ptr' are written into 'expected' and
 * 'false' is returned.
 * This includes a general memory barrier.
 */
bool thread_atomic_compare_exchange_i64(i64* ptr, i64* expected, i64 value);

/**
 * Atomically store the result of adding the value to the content of the given pointer at the
 * pointer address and returns the old value.
 * This includes a general memory barrier.
 */
i64 thread_atomic_add_i64(i64*, i64 value);

/**
 * Atomically store the result of substracting the value to the content of the given pointer at the
 * pointer address and returns the old value.
 * This includes a general memory barrier.
 */
i64 thread_atomic_sub_i64(i64*, i64 value);

/**
 * Start a new execution thread.
 * - 'data' is provided as an argument to the ThreadRoutine.
 * - 'threadName' can be retrieved as 'g_thread_name' in the new thread.
 * - Threads should be cleaned up by calling 'thread_join'.
 *
 * Pre-condition: threadName.size <= 15
 */
ThreadHandle thread_start(ThreadRoutine, void* data, String threadName);

/**
 * Wait for the given thread to finish and clean up its resources.
 * Note: Should be called exactly once per started thread.
 */
void thread_join(ThreadHandle);

/**
 * Stop executing the current thread and move it to the bottom of the run queue.
 */
void thread_yield();

/**
 * Sleep the current thread.
 */
void thread_sleep(TimeDuration);

/**
 * Create a new mutex.
 * Should be cleaned up using 'thread_mutex_destroy'.
 */
ThreadMutex thread_mutex_create(Allocator*);

/**
 * Free all resources associated with a mutex.
 * Pre-condition: Mutex is unlocked (not locked).
 */
void thread_mutex_destroy(ThreadMutex);

/**
 * Lock a mutex.
 * if the mutex is currently unlocked then this returns immediately, if the mutex is currently
 * locked by another thread then this blocks until that thread unlocks the mutex.
 * Pre-condition: Mutex is not being held by this thread.
 */
void thread_mutex_lock(ThreadMutex);

/**
 * Attempt to lock a mutex.
 * if the mutex is currently unlocked then 'true' is returned and the mutex is locked,
 * otherwise 'false' is returned.
 * Pre-condition: Mutex is not being held by this thread.
 */
bool thread_mutex_trylock(ThreadMutex);

/**
 * Unlock a mutex.
 * Pre-condition: Mutex is being held by this thread.
 */
void thread_mutex_unlock(ThreadMutex);
