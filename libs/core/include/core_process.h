#pragma once
#include "core_signal.h"
#include "core_string.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

typedef i64 ProcessId;

typedef enum {
  ProcessFlags_PipeStdIn  = 1 << 0, // Create a pipe for writing to std in.
  ProcessFlags_PipeStdOut = 1 << 1, // Create a pipe for reading from std out.
  ProcessFlags_PipeStdErr = 1 << 2, // Create a pipe for reading from std err.
  ProcessFlags_NewGroup   = 1 << 3, // Create a new process group for the child proccess.
} ProcessFlags;

/**
 * Process.
 */
typedef struct sProcess Process;

/**
 * Create a new process.
 * Destroy using 'process_destroy()'.
 */
Process* process_create(Allocator*, ProcessFlags);

/**
 * Destroy a process.
 */
void process_destroy(Process*);

void      process_kill(Process*);
i32       process_block(Process*);
void      process_signal(Process*, Signal);
ProcessId process_id(const Process*);
