#pragma once
#include "core_annotation.h"
#include "core_string.h"
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
