#pragma once
#include "core_annotation.h"
#include "core_string.h"
#include "core_time.h"
#include "core_types.h"
#include <stdatomic.h>

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
 * Atomic data types.
 * Read and writes will be atomic and can be used with operations which provide ordering guarantees.
 *
 * For atomic operations see the C11 <stdatomic.h> library.
 */
#define atomic_i8 _Atomic i8
#define atomic_i16 _Atomic i16
#define atomic_i32 _Atomic i32
#define atomic_i64 _Atomic i64
#define atomic_u8 _Atomic u8
#define atomic_u16 _Atomic u16
#define atomic_u32 _Atomic u32
#define atomic_u64 _Atomic u64
#define atomic_usize _Atomic usize

/**
 * Function to run on an execution thread.
 */
typedef void (*ThreadRoutine)(void*);

/**
 * Handle to a started thread.
 * Note: Thread resources should be cleaned up by calling 'thread_join'.
 */
typedef iptr ThreadHandle;

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
