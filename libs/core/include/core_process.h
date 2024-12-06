#pragma once
#include "core_signal.h"
#include "core_string.h"

/**
 * Process handle.
 */
typedef struct sProcess Process;

typedef i64 ProcessId;

typedef enum {
  ProcessFlags_None       = 0,
  ProcessFlags_PipeStdIn  = 1 << 0, // Create a pipe for writing to std-in.
  ProcessFlags_PipeStdOut = 1 << 1, // Create a pipe for reading from std-out.
  ProcessFlags_PipeStdErr = 1 << 2, // Create a pipe for reading from std-err.
  ProcessFlags_NewGroup   = 1 << 3, // Create a new process group for the child proccess.
  ProcessFlags_Detached   = 1 << 4, // Leave the process running when closing the handle.
  ProcessFlags_PipeAny = ProcessFlags_PipeStdIn | ProcessFlags_PipeStdOut | ProcessFlags_PipeStdErr,
} ProcessFlags;

typedef enum {
  ProcessResult_Success,
  ProcessResult_LimitReached,
  ProcessResult_TooManyArguments,
  ProcessResult_FailedToCreatePipe,
  ProcessResult_InvalidProcess,
  ProcessResult_NoPermission,
  ProcessResult_NotRunning,
  ProcessResult_ExecutableNotFound,
  ProcessResult_InvalidExecutable,
  ProcessResult_UnknownError,

  ProcessResult_Count,
} ProcessResult;

typedef enum {
  ProcessExitCode_Success                    = 0,
  ProcessExitCode_FailedToCreateProcessGroup = 200,
  ProcessExitCode_FailedToSetupPipes         = 201,
  ProcessExitCode_OutOfMemory                = 202,
  ProcessExitCode_ExecutableNotFound         = 203,
  ProcessExitCode_InvalidExecutable          = 204,
  ProcessExitCode_UnknownExecError           = 205,
  ProcessExitCode_InvalidProcess             = -1000,
  ProcessExitCode_TerminatedBySignal         = -1001,
  ProcessExitCode_UnknownError               = -1002,
} ProcessExitCode;

/**
 * Return a textual representation of the given ProcessResult.
 */
String process_result_str(ProcessResult);

/**
 * Create a new process handle.
 * Destroy using 'process_destroy()'.
 */
Process* process_create(Allocator*, String file, const String args[], u32 argCount, ProcessFlags);

/**
 * Destroy a process handle.
 * NOTE: Kills the process if its still running and the handle does not have the Detached flag.
 */
void process_destroy(Process*);

/**
 * Retrieve the result enum for starting the process.
 */
ProcessResult process_start_result(const Process*);

/**
 * Retrieve the OS process-id for this handle.
 */
ProcessId process_id(const Process*);

/**
 * Check if the given process is still running.
 */
bool process_poll(Process*);

/**
 * Retrieve a file handle to process pipes.
 * NOTE: Returns null if the process failed to start.
 * Pre-condition:  he process was started with the corresponding pipe flag.
 */
File* process_pipe_in(Process*);
File* process_pipe_out(Process*);
File* process_pipe_err(Process*);

/**
 * Close the input pipe, no further data can be written.
 */
void process_pipe_close_in(Process*);

/**
 * Send a signal to the given process.
 */
ProcessResult process_signal(Process*, Signal);

/**
 * Block until the process has finished executing and return its exit-code.
 * NOTE: -1000, -1001 and -1002 have special meaning given by this function.
 */
ProcessExitCode process_block(Process*);
