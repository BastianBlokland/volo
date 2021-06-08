#pragma once
#include "core_annotation.h"
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
