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
  Allocator*          alloc;
  ProcessFlags        flags : 8;
  ProcessResult       startResult : 8;
  bool                inputPipeClosed;
  PROCESS_INFORMATION processInfo;
  File                pipes[ProcessPipe_Count];
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
  if (process->flags & ProcessFlags_PipeStdIn && !process->inputPipeClosed) {
    CloseHandle(process->pipes[ProcessPipe_StdIn].handle);
  }
  if (process->flags & ProcessFlags_PipeStdOut) {
    CloseHandle(process->pipes[ProcessPipe_StdOut].handle);
  }
  if (process->flags & ProcessFlags_PipeStdErr) {
    CloseHandle(process->pipes[ProcessPipe_StdErr].handle);
  }
  if (process->processInfo.hThread) {
    CloseHandle(process->processInfo.hThread);
  }
  if (process->processInfo.hProcess) {
    CloseHandle(process->processInfo.hProcess);
  }
  alloc_free_t(process->alloc, process);
}

ProcessResult process_start_result(const Process* process) { return process->startResult; }

ProcessId process_id(const Process* process) {
  return process->startResult == ProcessResult_Success ? process->processInfo.dwProcessId : -1;
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
  CloseHandle(process->pipes[ProcessPipe_StdIn].handle);
}

ProcessResult process_signal(Process* process, const Signal signal) {
  const HANDLE handle = process->processInfo.hProcess;
  if (UNLIKELY(!handle)) {
    return ProcessExitCode_InvalidProcess;
  }
  switch (signal) {
  case Signal_Interrupt:
    /**
     * NOTE: Send 'CTRL_BREAK' instead of 'CTRL_C' because we cannot send ctrl-c to other process
     * groups (and we don't want to interrupt our entire own process-group).
     */
    if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, process->processInfo.dwProcessId)) {
      return ProcessResult_UnknownError;
    }
    return ProcessResult_Success;
  case Signal_Kill:
    if (!TerminateProcess(handle, (UINT)ProcessExitCode_TerminatedBySignal)) {
      switch (GetLastError()) {
      case ERROR_ACCESS_DENIED:
        return ProcessResult_NotRunning;
      default:
        return ProcessResult_UnknownError;
      }
    }
    return ProcessResult_Success;
  case Signal_Count:
    break;
  }
  return ProcessResult_UnknownError;
}

ProcessExitCode process_block(Process* process) {
  const HANDLE handle = process->processInfo.hProcess;
  if (UNLIKELY(!handle)) {
    return ProcessExitCode_InvalidProcess;
  }
  WaitForSingleObject(handle, INFINITE);
  DWORD status;
  if (!GetExitCodeProcess(handle, &status)) {
    return ProcessExitCode_UnknownError;
  }
  diag_assert(status != STILL_ACTIVE);
  return (ProcessExitCode)status;
}
