#pragma once
#include "core_signal.h"
#include "core_string.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'core_file.h'.
typedef struct sFile File;

typedef i64 ProcessId;

typedef enum {
  ProcessFlags_PipeStdIn  = 1 << 0, // Create a pipe for writing to std in.
  ProcessFlags_PipeStdOut = 1 << 1, // Create a pipe for reading from std out.
  ProcessFlags_PipeStdErr = 1 << 2, // Create a pipe for reading from std err.
  ProcessFlags_NewGroup   = 1 << 3, // Create a new process group for the child proccess.
  ProcessFlags_Detached   = 1 << 4, // Leave the process running when closing the handle.
} ProcessFlags;

typedef enum {
  ProcessResult_Success,
  ProcessResult_LimitReached,
  ProcessResult_TooManyArguments,
  ProcessResult_FailedToCreatePipe,
  ProcessResult_InvalidProcess,
  ProcessResult_NoPermission,
  ProcessResult_NotRunning,
  ProcessResult_UnknownError,

  ProcessResult_Count,
} ProcessResult;

typedef enum {
  ProcessExitCode_Success            = 0,
  ProcessExitCode_InvalidProcess     = -100,
  ProcessExitCode_TerminatedBySignal = -101,
  ProcessExitCode_UnknownError       = -102,
} ProcessExitCode;

/**
 * Process.
 */
typedef struct sProcess Process;

/**
 * Create a new process.
 * Destroy using 'process_destroy()'.
 */
Process* process_create(Allocator*, String file, const String args[], u32 argCount, ProcessFlags);

/**
 * Destroy a process.
 */
void process_destroy(Process*);

ProcessResult process_start_result(const Process*);
ProcessId     process_id(const Process*);

File* process_pipe_in(Process*);
File* process_pipe_out(Process*);
File* process_pipe_err(Process*);

ProcessResult   process_signal(Process*, Signal);
ProcessExitCode process_block(Process*);
