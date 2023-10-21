#include "core_alloc.h"
#include "core_diag.h"
#include "core_process.h"

#include "file_internal.h"

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#define process_args_max 128

struct sProcess {
  Allocator*    alloc;
  ProcessFlags  flags : 8;
  ProcessResult startResult : 8;
  pid_t         handle;
  File          pipes[3];
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
  case Signal_Interrupt:
    return SIGINT;
  case Signal_Kill:
    return SIGKILL;
  case Signal_Count:
    break;
  }
  diag_crash_msg("Invalid signal");
}

typedef struct {
  ProcessFlags  flags;
  String        file;
  const String* args;
  u32           argCount;
} ProcessStartInfo;

static usize process_start_file_and_arg_size(const ProcessStartInfo* info) {
  usize result = info->file.size + 1; // +1 for null-terminator.
  for (u32 i = 0; i != info->argCount; ++i) {
    result += info->args[i].size + 1; // +1 for null-terminator.
  }
  return result;
}

static Mem process_null_terminate(const Mem buffer, const String str, char** out) {
  diag_assert(buffer.size > str.size);

  mem_cpy(buffer, str);
  *mem_at_u8(buffer, str.size) = '\0'; // Null terminate the string.

  *out = buffer.ptr;
  return mem_consume(buffer, str.size + 1);
}

NORETURN static void process_child_exec(const ProcessStartInfo* info) {
  if (info->flags & ProcessFlags_NewGroup) {
    const pid_t newSession = setsid(); // Create a new session (with a new progress group).
    if (UNLIKELY(newSession == -1)) {
      diag_crash_msg("[process error] Failed to create a new process group\n");
    }
  }

  // TODO: Close the parent side of the pipes (if they are created).
  // TODO: Duplicate the child side of the pipes onto stdIn, stdOut and stdErr of this process.

  /**
   * Convert both file and the arguments to null-terminated strings for exec, and also
   * null-terminate the arguments array it self.
   * NOTE: Note the memory does not need to be freed as exec will free the whole address space.
   */
  const usize fileAndArgSize   = process_start_file_and_arg_size(info);
  Mem         fileAndArgBuffer = alloc_alloc(g_alloc_heap, fileAndArgSize, 1);

  char* file;
  char* argv[process_args_max + 1];

  fileAndArgBuffer = process_null_terminate(fileAndArgBuffer, info->file, &file);
  for (u32 i = 0; i != info->argCount; ++i) {
    fileAndArgBuffer = process_null_terminate(fileAndArgBuffer, info->args[i], &argv[i]);
  }
  argv[info->argCount] = null; // Null terminate the array.

  // Execute the target file (will replace this process's image).
  execvp(file, argv);

  // An error occurred (this path is only reachable if exec failed).
  switch (errno) {
  case ENOENT:
    diag_crash_msg("[process error] Executable not found: {}", fmt_text(info->file));
  case EACCES:
    diag_crash_msg("[process error] Access to executable denied: {}", fmt_text(info->file));
  case EINVAL:
    diag_crash_msg("[process error] Invalid executable: {}", fmt_text(info->file));
  case ENOMEM:
    diag_crash_msg("[process error] Out of memory");
  default:
    diag_crash_msg("[process error] Unknown error while executing: {}", fmt_text(info->file));
  }
}

static ProcessResult process_start(const ProcessStartInfo* info, pid_t* outHandle) {
  if (UNLIKELY(info->argCount > process_args_max)) {
    return ProcessResult_ProcessTooManyArguments;
  }
  const pid_t forkedPid = fork();
  if (forkedPid == 0) {
    process_child_exec(info);
  }
  // TODO: Close the child side of the pipes (if they exist).
  if (UNLIKELY(forkedPid < 0)) {
    switch (errno) {
    case EAGAIN:
      return ProcessResult_ProcessLimitReached;
    default:
      return ProcessResult_UnknownError;
    }
  }
  *outHandle = forkedPid;
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
  process->startResult = process_start(&startInfo, &process->handle);

  return process;
}

void process_destroy(Process* itr) { alloc_free_t(itr->alloc, itr); }

ProcessResult process_start_result(const Process* process) { return process->startResult; }

ProcessId process_id(const Process* process) {
  return process->startResult == ProcessResult_Success ? process->handle : -1;
}

File* process_pipe_in(Process* process) {
  if (process->startResult == ProcessResult_Success && process->flags & ProcessFlags_PipeStdIn) {
    return &process->pipes[0];
  }
  return null;
}

File* process_pipe_out(Process* process) {
  if (process->startResult == ProcessResult_Success && process->flags & ProcessFlags_PipeStdOut) {
    return &process->pipes[1];
  }
  return null;
}

File* process_pipe_err(Process* process) {
  if (process->startResult == ProcessResult_Success && process->flags & ProcessFlags_PipeStdErr) {
    return &process->pipes[2];
  }
  return null;
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
  int status;
  if (waitpid(proc, &status, 0) != proc) {
    return ProcessExitCode_UnknownError;
  }
  if (WIFEXITED(status)) {
    return (ProcessExitCode)WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return ProcessExitCode_TerminatedBySignal;
  }
  return ProcessExitCode_UnknownError;
}
