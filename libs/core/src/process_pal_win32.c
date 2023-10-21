#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_process.h"

#include "file_internal.h"

#include <Windows.h>

#define process_args_max 128

typedef enum {
  ProcessPipe_StdIn,
  ProcessPipe_StdOut,
  ProcessPipe_StdErr,

  ProcessPipe_Count
} ProcessPipe;

struct sProcess {
  Allocator*    alloc;
  ProcessFlags  flags : 8;
  ProcessResult startResult : 8;
  bool          inputPipeClosed;
  File          pipes[ProcessPipe_Count];
};

typedef struct {
  ProcessFlags  flags;
  String        file;
  const String* args;
  u32           argCount;
} ProcessStartInfo;

static ProcessResult process_start(const ProcessStartInfo* info, File outPipes[3]) {
  if (UNLIKELY(info->argCount > process_args_max)) {
    return ProcessResult_TooManyArguments;
  }
  (void)outPipes;
  return ProcessResult_Success;
}

Process* process_create(
    Allocator*         alloc,
    const String       file,
    const String       args[],
    const u32          argCount,
    const ProcessFlags flags) {
  Process* process = alloc_alloc_t(alloc, Process);
  *process         = (Process){.alloc = alloc, .flags = flags};

  const ProcessStartInfo startInfo = {
      .flags    = flags,
      .file     = file,
      .args     = args,
      .argCount = argCount,
  };
  process->startResult = process_start(&startInfo, process->pipes);

  return process;
}

void process_destroy(Process* process) {
  if (!(process->flags & ProcessFlags_Detached)) {
    process_signal(process, Signal_Kill);
    process_block(process); // Wait for process to stop, this prevents leaking zombie processes.
  }
  // TODO: Close pipes.
  alloc_free_t(process->alloc, process);
}

ProcessResult process_start_result(const Process* process) { return process->startResult; }

ProcessId process_id(const Process* process) {
  // TODO: Implement process id.
  return -1;
}

File* process_pipe_in(Process* process) {
  diag_assert_msg(process->flags & ProcessFlags_PipeStdIn, "Input not piped");
  if (process->startResult == ProcessResult_Success) {
    return &process->pipes[ProcessPipe_StdIn];
  }
  return null;
}

File* process_pipe_out(Process* process) {
  diag_assert_msg(process->flags & ProcessFlags_PipeStdOut, "Output not piped");
  if (process->startResult == ProcessResult_Success) {
    return &process->pipes[ProcessPipe_StdOut];
  }
  return null;
}

File* process_pipe_err(Process* process) {
  diag_assert_msg(process->flags & ProcessFlags_PipeStdErr, "Error not piped");
  if (process->startResult == ProcessResult_Success) {
    return &process->pipes[ProcessPipe_StdErr];
  }
  return null;
}

void process_pipe_close_in(Process* process) {
  diag_assert_msg(process->flags & ProcessFlags_PipeStdIn, "Input not piped");
  diag_assert_msg(!process->inputPipeClosed, "Input pipe already closed");
  process->pipes[ProcessPipe_StdIn].access = FileAccess_None;
  process->inputPipeClosed                 = true;
  // TODO: Close input pipe.
}

ProcessResult process_signal(Process* process, const Signal signal) {
  (void)process;
  (void)signal;
  // TODO: Support signals.
  return ProcessResult_Success;
}

ProcessExitCode process_block(Process* process) {
  (void)process;
  // TODO: Support blocking.
  return ProcessResult_Success;
}
