#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_process.h"
#include "core_signal.h"
#include "core_winutils.h"

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

static void process_maybe_close_handle(HANDLE handle) {
  if (handle) {
    CloseHandle(handle);
  }
}

static void process_maybe_close_handles(HANDLE handles[], const u32 count) {
  for (u32 i = 0; i != count; ++i) {
    process_maybe_close_handle(handles[i]);
  }
}

typedef struct {
  ProcessFlags  flags;
  String        file;
  const String* args;
  u32           argCount;
} ProcessStartInfo;

static void process_build_cmdline(DynString* out, const ProcessStartInfo* info) {
  dynstring_append_char(out, '"');
  dynstring_append(out, info->file);
  dynstring_append_char(out, '"');

  for (u32 i = 0; i != info->argCount; ++i) {
    dynstring_append(out, string_lit(" \""));
    dynstring_append(out, info->args[i]);
    dynstring_append_char(out, '"');
  }
}

#define PIPE_HND_READ(_HNDS_, _PIPE_) ((_HNDS_)[ProcessPipe_##_PIPE_ * 2 + 0])
#define PIPE_HND_WRITE(_HNDS_, _PIPE_) ((_HNDS_)[ProcessPipe_##_PIPE_ * 2 + 1])

static ProcessResult
process_start(const ProcessStartInfo* info, PROCESS_INFORMATION* outProcessInfo, File outPipes[3]) {
  if (UNLIKELY(info->argCount > process_args_max)) {
    return ProcessResult_TooManyArguments;
  }

  // 2 handles (both ends of the pipe) for stdIn, stdOut and stdErr.
  HANDLE pipeHandles[ProcessPipe_Count * 2] = {0};

  const DWORD         pipeBufferSize = 0; // Use system default.
  SECURITY_ATTRIBUTES pipeAttr       = {
            .nLength        = sizeof(SECURITY_ATTRIBUTES),
            .bInheritHandle = true,
  };

  // clang-format off
  bool pipeFail = false;
  pipeFail |= info->flags & ProcessFlags_PipeStdIn  && !CreatePipe(&PIPE_HND_READ(pipeHandles, StdIn), &PIPE_HND_WRITE(pipeHandles, StdIn), &pipeAttr, pipeBufferSize);
  pipeFail |= info->flags & ProcessFlags_PipeStdOut && !CreatePipe(&PIPE_HND_READ(pipeHandles, StdOut), &PIPE_HND_WRITE(pipeHandles, StdOut), &pipeAttr, pipeBufferSize);
  pipeFail |= info->flags & ProcessFlags_PipeStdErr && !CreatePipe(&PIPE_HND_READ(pipeHandles, StdErr), &PIPE_HND_WRITE(pipeHandles, StdErr), &pipeAttr, pipeBufferSize);
  // clang-format on
  if (UNLIKELY(pipeFail)) {
    // Close the handles of the pipes we did manage to create.
    process_maybe_close_handles(pipeHandles, array_elems(pipeHandles));

    return ProcessResult_FailedToCreatePipe;
  }

  size_t attrListSize;
  InitializeProcThreadAttributeList(null, 1, 0, &attrListSize);
  LPPROC_THREAD_ATTRIBUTE_LIST attrList = alloc_alloc(g_allocHeap, attrListSize, sizeof(uptr)).ptr;
  if (UNLIKELY(!attrList || !InitializeProcThreadAttributeList(attrList, 1, 0, &attrListSize))) {
    process_maybe_close_handles(pipeHandles, array_elems(pipeHandles));
    return ProcessResult_UnknownError;
  }

  HANDLE handlesToInherit[ProcessPipe_Count];
  u32    handlesToInheritCount = 0;
  if (info->flags & ProcessFlags_PipeStdIn) {
    handlesToInherit[handlesToInheritCount++] = PIPE_HND_READ(pipeHandles, StdIn);
  }
  if (info->flags & ProcessFlags_PipeStdOut) {
    handlesToInherit[handlesToInheritCount++] = PIPE_HND_WRITE(pipeHandles, StdOut);
  }
  if (info->flags & ProcessFlags_PipeStdErr) {
    handlesToInherit[handlesToInheritCount++] = PIPE_HND_WRITE(pipeHandles, StdErr);
  }
  if (info->flags & ProcessFlags_PipeAny) {
    UpdateProcThreadAttribute(
        attrList,
        0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        handlesToInherit,
        sizeof(HANDLE) * handlesToInheritCount,
        null,
        null);
  }

  STARTUPINFOEX startupInfoEx = {
      .StartupInfo.cb         = sizeof(startupInfoEx),
      .StartupInfo.hStdInput  = PIPE_HND_READ(pipeHandles, StdIn),
      .StartupInfo.hStdOutput = PIPE_HND_WRITE(pipeHandles, StdOut),
      .StartupInfo.hStdError  = PIPE_HND_WRITE(pipeHandles, StdErr),
      .lpAttributeList        = attrList,
  };
  if (info->flags & ProcessFlags_PipeAny) {
    startupInfoEx.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
  }

  DWORD creationFlags = NORMAL_PRIORITY_CLASS | EXTENDED_STARTUPINFO_PRESENT;
  if (info->flags & ProcessFlags_NewGroup) {
    creationFlags |= CREATE_NEW_PROCESS_GROUP;
  }

  DynString cmdLineScratch = dynstring_create(g_allocScratch, usize_kibibyte * 32);
  process_build_cmdline(&cmdLineScratch, info);
  Mem cmdLineWideScratch = winutils_to_widestr_scratch(dynstring_view(&cmdLineScratch));

  const bool success = CreateProcess(
      null,
      (LPWSTR)cmdLineWideScratch.ptr,
      null,
      null,
      true,
      creationFlags,
      null,
      null,
      &startupInfoEx.StartupInfo,
      outProcessInfo);

  if (success) {
    // Success; close only the child side of the pipes.
    process_maybe_close_handle(PIPE_HND_READ(pipeHandles, StdIn));
    process_maybe_close_handle(PIPE_HND_WRITE(pipeHandles, StdOut));
    process_maybe_close_handle(PIPE_HND_WRITE(pipeHandles, StdErr));
  } else {
    // Failure; close both sides of all the pipes.
    process_maybe_close_handles(pipeHandles, array_elems(pipeHandles));

    // TODO: Do these need closing in case of failure?
    CloseHandle(outProcessInfo->hThread);
    CloseHandle(outProcessInfo->hProcess);
  }

  if (attrList) {
    DeleteProcThreadAttributeList(attrList);
    alloc_free(g_allocHeap, mem_create(attrList, attrListSize));
  }

  if (!success) {
    switch (GetLastError()) {
    case ERROR_NOACCESS:
      return ProcessResult_NoPermission;
    case ERROR_INVALID_HANDLE:
      return ProcessResult_ExecutableNotFound;
    case ERROR_INVALID_STARTING_CODESEG:
    case ERROR_INVALID_STACKSEG:
    case ERROR_INVALID_MODULETYPE:
    case ERROR_INVALID_EXE_SIGNATURE:
    case ERROR_EXE_MARKED_INVALID:
    case ERROR_BAD_EXE_FORMAT:
      return ProcessResult_InvalidExecutable;
    default:
      return ProcessResult_UnknownError;
    }
  }

  if (info->flags & ProcessFlags_PipeStdIn) {
    outPipes[0] = (File){.handle = PIPE_HND_WRITE(pipeHandles, StdIn), .access = FileAccess_Write};
  }
  if (info->flags & ProcessFlags_PipeStdOut) {
    outPipes[1] = (File){.handle = PIPE_HND_READ(pipeHandles, StdOut), .access = FileAccess_Read};
  }
  if (info->flags & ProcessFlags_PipeStdErr) {
    outPipes[2] = (File){.handle = PIPE_HND_READ(pipeHandles, StdErr), .access = FileAccess_Read};
  }
  return ProcessResult_Success;
}

#undef PIPE_HND_READ
#undef PIPE_HND_WRITE

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
  process->startResult = process_start(&startInfo, &process->processInfo, process->pipes);

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
  return process->startResult == ProcessResult_Success ? (i64)process->processInfo.dwProcessId : -1;
}

bool process_poll(Process* process) {
  const HANDLE handle = process->processInfo.hProcess;
  if (UNLIKELY(!handle)) {
    return false;
  }
  return WaitForSingleObject(handle, 0) != WAIT_OBJECT_0;
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
    return ProcessResult_InvalidProcess;
  }
  switch (signal) {
  case Signal_Terminate:
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
  if (UNLIKELY(process->startResult == ProcessResult_ExecutableNotFound)) {
    return ProcessExitCode_ExecutableNotFound;
  }
  if (UNLIKELY(process->startResult == ProcessResult_InvalidExecutable)) {
    return ProcessExitCode_InvalidExecutable;
  }
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
