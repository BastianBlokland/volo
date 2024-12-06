#pragma once
#include "core.h"
#include "core_string.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'core_time.h'.
typedef i64 TimeDuration;

/**
 * Unique identifier for a thread.
 * NOTE: Thread-ids can be recycled by the kernel so a new thread might be created with the same id.
 */
typedef i32 ThreadId;

/**
 * Process identifier (aka 'thread group id').
 * The same value for all threads.
 */
extern ThreadId g_threadPid;

/**
 * Thread identifier of the main thread.
 * NOTE: The thread that calls 'core_init()' is considered the main thread.
 */
extern ThreadId g_threadMainTid;

/**
 * Thread identifier of the current thread.
 */
extern THREAD_LOCAL ThreadId g_threadTid;

/**
 * Name of the current thread.
 */
extern THREAD_LOCAL String g_threadName;

/**
 * Address of the top of the stack of the current thread.
 */
extern THREAD_LOCAL uptr g_threadStackTop;

/**
 * Number of logical cpu cores available to this process.
 */
extern u16 g_threadCoreCount;

/**
 * Function to run on an execution thread.
 */
typedef void (*ThreadRoutine)(void*);

/**
 * Handle to a started thread.
 * NOTE: Thread resources should be cleaned up by calling 'thread_join()'.
 */
typedef uptr ThreadHandle;

/**
 * Handle to a mutex.
 * NOTE: Should be cleaned up by calling 'thread_mutex_destroy()'.
 */
typedef uptr ThreadMutex;

/**
 * Handle to a condition.
 * NOTE: Should be cleaned up by calling 'thread_cond_destroy()'.
 */
typedef uptr ThreadCondition;

/**
 * SpinLock semaphore.
 * Useful for very short locks where the cost of context switching would be too high.
 * Lock using 'thread_spinlock_lock()', and unlock using 'thread_spinlock_unlock()'.
 * NOTE: Should be zero initialized.
 */
typedef i32 ThreadSpinLock;

/**
 * Atomically reads the value at the given pointer.
 * This includes a general memory barrier.
 */
i32 thread_atomic_load_i32(i32*);
u32 thread_atomic_load_u32(u32*);
i64 thread_atomic_load_i64(i64*);
u64 thread_atomic_load_u64(u64*);

/**
 * Atomically stores the value at the given pointer.
 * This includes a general memory barrier.
 */
void thread_atomic_store_i32(i32*, i32 value);
void thread_atomic_store_u32(u32*, u32 value);
void thread_atomic_store_i64(i64*, i64 value);
void thread_atomic_store_u64(u64*, u64 value);

/**
 * Atomically stores the value at the given pointer and returns the old value.
 * This includes a general memory barrier.
 */
i32 thread_atomic_exchange_i32(i32*, i32 value);
i64 thread_atomic_exchange_i64(i64*, i64 value);

/**
 * Compares the content of 'ptr' with the content of 'expected'. If equal the 'value' is stored into
 * 'ptr' and 'true' is returned. If not equal, the contents of 'ptr' are written into 'expected' and
 * 'false' is returned.
 * This includes a general memory barrier.
 */
bool thread_atomic_compare_exchange_i32(i32* ptr, i32* expected, i32 value);
bool thread_atomic_compare_exchange_i64(i64* ptr, i64* expected, i64 value);

/**
 * Atomically store the result of adding the value to the content of the given pointer at the
 * pointer address and returns the old value.
 * This includes a general memory barrier.
 */
i32 thread_atomic_add_i32(i32*, i32 value);
i64 thread_atomic_add_i64(i64*, i64 value);

/**
 * Atomically store the result of subtracting the value to the content of the given pointer at the
 * pointer address and returns the old value.
 * This includes a general memory barrier.
 */
i32 thread_atomic_sub_i32(i32*, i32 value);
i64 thread_atomic_sub_i64(i64*, i64 value);

/**
 * Enforce a strong memory load / store order before and after this call.
 */
void thread_atomic_fence(void);

/**
 * Enforce a strong memory load / store order before and after matching acquire / release
 * pairs (or other atomic operations).
 */
void thread_atomic_fence_acquire(void);
void thread_atomic_fence_release(void);

typedef enum {
  ThreadPriority_Lowest,
  ThreadPriority_Low,
  ThreadPriority_Normal,
  ThreadPriority_High,
  ThreadPriority_Highest,
} ThreadPriority;

/**
 * Start a new execution thread.
 * - 'data' is provided as an argument to the ThreadRoutine.
 * - 'threadName' can be retrieved as 'g_threadName' in the new thread.
 * - Threads should be cleaned up by calling 'thread_join'.
 *
 * Pre-condition: threadName.size <= 15
 */
ThreadHandle thread_start(ThreadRoutine, void* data, String threadName, ThreadPriority);

/**
 * Set the priority of the current thread.
 * Returns true if successful otherwise false.
 * NOTE: Can fail if the user has insufficent permissions.
 */
bool thread_prioritize(ThreadPriority);

/**
 * Wait for the given thread to finish and clean up its resources.
 * NOTE: Should be called exactly once per started thread.
 */
void thread_join(ThreadHandle);

/**
 * Stop executing the current thread and move it to the bottom of the run queue.
 */
void thread_yield(void);

/**
 * Sleep the current thread.
 */
void thread_sleep(TimeDuration);

/**
 * Check if a thread exists with the given id.
 */
bool thread_exists(ThreadId);

/**
 * Create a new mutex.
 * Should be cleaned up using 'thread_mutex_destroy()'.
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

/**
 * Create a new condition.
 * Should be cleaned up using 'thread_cond_destroy()'.
 */
ThreadCondition thread_cond_create(Allocator*);

/**
 * Free all resources associated with a condition.
 * Pre-condition: No threads are waiting on this condition.
 */
void thread_cond_destroy(ThreadCondition);

/**
 * Wait for the condition to be signaled by another thread. The mutex is automatically unlocked
 * before the wait and re-locked after returning from the wait.
 *
 * Example usage:
 * '
 *  thread_mutex_lock(&myMutex);
 *  while (!myPredicate()) {
 *    thread_cond_wait(&myCond, &myMutex);
 *  }
 *  thread_mutex_unlock(&myMutex);
 * '
 * Pre-condition: This thread is currently holding the mutex.
 */
void thread_cond_wait(ThreadCondition, ThreadMutex);

/**
 * Unblock atleast one thread waiting for the given condition.
 * NOTE: It is possible that more then one thread is woken up, often called 'spurious wakeup'.
 */
void thread_cond_signal(ThreadCondition);

/**
 * Unblock all threads waiting for the given condition.
 */
void thread_cond_broadcast(ThreadCondition);

/**
 * Acquire the spinlink.
 * In order to avoid wasting resources this lock should be held for a short as possible.
 * This includes a general memory barrier that synchronizes with 'thread_spinlock_unlock()'.
 *
 * Pre-condition: SpinLock is not being held by this thread.
 */
void thread_spinlock_lock(ThreadSpinLock*);

/**
 * Release the spinlink.
 * This includes a general memory barrier that synchronizes with 'thread_spinlock_lock()'.
 *
 * Pre-condition: Spinlock is being held by this thread.
 */
void thread_spinlock_unlock(ThreadSpinLock*);
