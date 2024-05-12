#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_process.h"

#include "file_internal.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

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
  bool          terminated;
  bool          inputPipeClosed;
  pid_t         handle;
  int           terminationStatus;
  File          pipes[ProcessPipe_Count];
};

static ProcessResult process_result_from_errno(const int err) {
  switch (err) {
  case EPERM:
    return ProcessResult_NoPermission;
  case ESRCH:
    return ProcessResult_NotRunning;
  }
  return ProcessResult_UnknownError;
}

static int process_signal_code(const Signal signal) {
  switch (signal) {
  case Signal_Terminate:
    return SIGTERM;
  case Signal_Interrupt:
    return SIGINT;
  case Signal_Kill:
    return SIGKILL;
  case Signal_Count:
    break;
  }
  diag_crash_msg("Invalid signal");
}

static void process_maybe_close_fd(const int fd) {
  if (fd == -1) {
    return; // Sentinel we use to indicate an unused file-descriptor.
  }
TryClose:
  if (UNLIKELY(close(fd) < 0)) {
    switch (errno) {
    case EBADF:
      diag_crash_msg("Failed to close invalid file-descriptor: {}", fmt_int(fd));
    case EINTR:
      goto TryClose; // Interrupted; retry.
    default:
      diag_crash_msg("Unknown error while closing file-descriptor: {}", fmt_int(fd));
    }
  }
}

static void process_maybe_close_fds(const int fds[], const u32 count) {
  for (u32 i = 0; i != count; ++i) {
    process_maybe_close_fd(fds[i]);
  }
}

typedef struct {
  ProcessFlags  flags;
  String        file;
  const String* args;
  u32           argCount;
} ProcessStartInfo;

static usize process_start_arg_null_term_size(const ProcessStartInfo* info) {
  usize result = info->file.size + 1; // +1 for null-terminator.
  for (u32 i = 0; i != info->argCount; ++i) {
    result += info->args[i].size + 1; // +1 for null-terminator.
  }
  return result;
}

static Mem process_null_term(const Mem buffer, const String str, char** out) {
  diag_assert(buffer.size > str.size);

  mem_cpy(buffer, str);
  *mem_at_u8(buffer, str.size) = '\0'; // Null terminate the string.

  *out = buffer.ptr;
  return mem_consume(buffer, str.size + 1);
}

#define PIPE_FD_READ(_FDS_, _PIPE_) ((_FDS_)[ProcessPipe_##_PIPE_ * 2 + 0])
#define PIPE_FD_WRITE(_FDS_, _PIPE_) ((_FDS_)[ProcessPipe_##_PIPE_ * 2 + 1])

NORETURN static void process_child_abort(const ProcessExitCode code) {
  // NOTE: Do not use the lib-c 'exit' as we do not want to fire lib-c 'atexit' functions.
  syscall(SYS_exit, code);
  UNREACHABLE
}

NORETURN static void process_child_exec(const ProcessStartInfo* info, const int pipeFds[]) {
  if (info->flags & ProcessFlags_NewGroup) {
    const pid_t newSession = setsid(); // Create a new session (with a new progress group).
    if (UNLIKELY(newSession == -1)) {
      process_child_abort(ProcessExitCode_FailedToCreateProcessGroup);
    }
  }

  // Close the parent side of the pipes.
  process_maybe_close_fd(PIPE_FD_WRITE(pipeFds, StdIn));
  process_maybe_close_fd(PIPE_FD_READ(pipeFds, StdOut));
  process_maybe_close_fd(PIPE_FD_READ(pipeFds, StdErr));

  // Duplicate the child side of the pipes onto stdIn, stdOut and stdErr of this process.
  bool dupFail = false;
  dupFail |= info->flags & ProcessFlags_PipeStdIn && dup2(PIPE_FD_READ(pipeFds, StdIn), 0) == -1;
  dupFail |= info->flags & ProcessFlags_PipeStdOut && dup2(PIPE_FD_WRITE(pipeFds, StdOut), 1) == -1;
  dupFail |= info->flags & ProcessFlags_PipeStdErr && dup2(PIPE_FD_WRITE(pipeFds, StdErr), 2) == -1;
  if (UNLIKELY(dupFail)) {
    process_child_abort(ProcessExitCode_FailedToSetupPipes);
  }

  /**
   * Convert both file and the arguments to null-terminated strings for exec, and also
   * null-terminate the arguments array it self.
   * NOTE: File is appended as the first argument.
   * NOTE: The memory does not need to be freed as exec will free the whole address space.
   */
  const usize argSize   = process_start_arg_null_term_size(info);
  Mem         argBuffer = alloc_alloc(g_alloc_heap, argSize, 1);
  if (!mem_valid(argBuffer)) {
    if (info->flags & ProcessPipe_StdErr) {
      diag_print_err("[process error] Out of memory");
    }
    process_child_abort(ProcessExitCode_OutOfMemory);
  }

  char* argv[process_args_max + 2]; // +1 for file and +1 null terminator.
  argBuffer = process_null_term(argBuffer, info->file, &argv[0]);
  for (u32 i = 0; i != info->argCount; ++i) {
    argBuffer = process_null_term(argBuffer, info->args[i], &argv[i + 1]);
  }
  argv[info->argCount + 1] = null; // Null terminate the array.

  // Execute the target file (will replace this process's image).
  execvp(argv[0], argv);

  // An error occurred (this path is only reachable if exec failed).
  switch (errno) {
  case ENOENT:
    if (info->flags & ProcessPipe_StdErr) {
      diag_print_err("[process error] Executable not found: {}\n", fmt_text(info->file));
    }
    process_child_abort(ProcessExitCode_ExecutableNotFound);
  case EACCES:
  case EINVAL:
    if (info->flags & ProcessPipe_StdErr) {
      diag_print_err("[process error] Invalid executable: {}\n", fmt_text(info->file));
    }
    process_child_abort(ProcessExitCode_InvalidExecutable);
  case ENOMEM:
    if (info->flags & ProcessPipe_StdErr) {
      diag_print_err("[process error] Out of memory\n");
    }
    process_child_abort(ProcessExitCode_OutOfMemory);
  default:
    if (info->flags & ProcessPipe_StdErr) {
      diag_print_err("[process error] Unknown error while executing: {}\n", fmt_text(info->file));
    }
    process_child_abort(ProcessExitCode_UnknownExecError);
  }
}

static ProcessResult process_start(const ProcessStartInfo* info, pid_t* outPid, File outPipes[3]) {
  if (UNLIKELY(info->argCount > process_args_max)) {
    return ProcessResult_TooManyArguments;
  }

  // 2 file-descriptors (both ends of the pipe) for stdIn, stdOut and stdErr.
  int pipeFds[ProcessPipe_Count * 2] = {-1, -1, -1, -1, -1, -1};

  bool pipeFail = false;
  pipeFail |= info->flags & ProcessFlags_PipeStdIn && pipe(pipeFds + ProcessPipe_StdIn * 2) != 0;
  pipeFail |= info->flags & ProcessFlags_PipeStdOut && pipe(pipeFds + ProcessPipe_StdOut * 2) != 0;
  pipeFail |= info->flags & ProcessFlags_PipeStdErr && pipe(pipeFds + ProcessPipe_StdErr * 2) != 0;
  if (UNLIKELY(pipeFail)) {
    // Close the file-descriptors of the pipes we did manage to create.
    process_maybe_close_fds(pipeFds, array_elems(pipeFds));

    return ProcessResult_FailedToCreatePipe;
  }

  const pid_t forkedPid = fork();
  if (forkedPid == 0) {
    process_child_exec(info, pipeFds);
  }

  if (UNLIKELY(forkedPid < 0)) {
    // Failed to fork, close both sides of all the pipes.
    process_maybe_close_fds(pipeFds, array_elems(pipeFds));

    switch (errno) {
    case EAGAIN:
      return ProcessResult_LimitReached;
    default:
      return ProcessResult_UnknownError;
    }
  }

  // Fork succeeded, close only the child side of the pipes.
  process_maybe_close_fd(PIPE_FD_READ(pipeFds, StdIn));
  process_maybe_close_fd(PIPE_FD_WRITE(pipeFds, StdOut));
  process_maybe_close_fd(PIPE_FD_WRITE(pipeFds, StdErr));

  *outPid = forkedPid;
  if (info->flags & ProcessFlags_PipeStdIn) {
    outPipes[0] = (File){.handle = PIPE_FD_WRITE(pipeFds, StdIn), .access = FileAccess_Write};
  }
  if (info->flags & ProcessFlags_PipeStdOut) {
    outPipes[1] = (File){.handle = PIPE_FD_READ(pipeFds, StdOut), .access = FileAccess_Read};
  }
  if (info->flags & ProcessFlags_PipeStdErr) {
    outPipes[2] = (File){.handle = PIPE_FD_READ(pipeFds, StdErr), .access = FileAccess_Read};
  }
  return ProcessResult_Success;
}

#undef PIPE_FD_READ
#undef PIPE_FD_WRITE

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
  process->startResult = process_start(&startInfo, &process->handle, process->pipes);

  return process;
}

void process_destroy(Process* process) {
  if (!process->terminated && !(process->flags & ProcessFlags_Detached)) {
    process_signal(process, Signal_Kill);
    process_block(process); // Wait for process to stop, this prevents leaking zombie processes.
  }
  if (process->flags & ProcessFlags_PipeStdIn && !process->inputPipeClosed) {
    process_maybe_close_fd(process->pipes[ProcessPipe_StdIn].handle);
  }
  if (process->flags & ProcessFlags_PipeStdOut) {
    process_maybe_close_fd(process->pipes[ProcessPipe_StdOut].handle);
  }
  if (process->flags & ProcessFlags_PipeStdErr) {
    process_maybe_close_fd(process->pipes[ProcessPipe_StdErr].handle);
  }
  alloc_free_t(process->alloc, process);
}

ProcessResult process_start_result(const Process* process) { return process->startResult; }

ProcessId process_id(const Process* process) {
  return process->startResult == ProcessResult_Success ? process->handle : -1;
}

bool process_poll(Process* process) {
  const pid_t proc = process->handle;
  if (proc <= 0 || process->terminated) {
    return false;
  }
  const int waitRes = waitpid(proc, &process->terminationStatus, WNOHANG);
  if (waitRes != 0) {
    process->terminated = true;
    return false;
  }
  return true;
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
  process_maybe_close_fd(process->pipes[ProcessPipe_StdIn].handle);
}

ProcessResult process_signal(Process* process, const Signal signal) {
  const pid_t proc = process->handle;
  if (UNLIKELY(proc <= 0)) {
    return ProcessResult_InvalidProcess;
  }
  const int code = process_signal_code(signal);
  if (process->flags & ProcessFlags_NewGroup) {
    const pid_t groupId = getpgid(proc);
    if (UNLIKELY(groupId < 0)) {
      return process_result_from_errno(errno);
    }
    return killpg(groupId, code) < 0 ? process_result_from_errno(errno) : ProcessResult_Success;
  }
  return kill(proc, code) < 0 ? process_result_from_errno(errno) : ProcessResult_Success;
}

ProcessExitCode process_block(Process* process) {
  const pid_t proc = process->handle;
  if (UNLIKELY(proc <= 0)) {
    return ProcessExitCode_InvalidProcess;
  }
  if (!process->terminated) {
    if (waitpid(proc, &process->terminationStatus, 0) != proc) {
      return ProcessExitCode_UnknownError;
    }
    process->terminated = true;
  }
  if (WIFEXITED(process->terminationStatus)) {
    return (ProcessExitCode)WEXITSTATUS(process->terminationStatus);
  }
  if (WIFSIGNALED(process->terminationStatus)) {
    return ProcessExitCode_TerminatedBySignal;
  }
  return ProcessExitCode_UnknownError;
}
