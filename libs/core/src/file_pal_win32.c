#include "core_diag.h"
#include "core_file.h"
#include "file_internal.h"
#include <Windows.h>

File* g_file_stdin;
File* g_file_stdout;
File* g_file_stderr;

void file_pal_init() {
  static File stdIn = {};
  stdIn.handle = GetStdHandle(STD_INPUT_HANDLE);
  if(stdIn.handle != INVALID_HANDLE_VALUE) {
    g_file_stdin = &stdIn;
  }

  static File stdOut = {};
  stdOut.handle = GetStdHandle(STD_OUTPUT_HANDLE);
  if(stdOut.handle != INVALID_HANDLE_VALUE) {
    g_file_stdout = &stdOut;
  }

  static File stdErr = {};
  stdErr.handle = GetStdHandle(STD_ERROR_HANDLE);
  if(stdErr.handle != INVALID_HANDLE_VALUE) {
    g_file_stderr = &stdErr;
  }
}

static FileResult file_result_from_lasterror() {
  switch (GetLastError()) {
  case ERROR_ACCESS_DENIED:
    return File_NoAccess;
  case ERROR_SHARING_VIOLATION:
    return File_Locked;
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
  case ERROR_INVALID_DRIVE:
    return File_NotFound;
  case ERROR_DISK_FULL:
    return File_DiskFull;
  case ERROR_TOO_MANY_OPEN_FILES:
    return File_TooManyOpenFiles;
  }
  return File_UnknownError;
}

FileResult file_write_sync(File* file, String data) {
  diag_assert(file);

  for (u8* itr = mem_begin(data); itr != mem_end(data);) {
    DWORD bytesWritten;
    if(WriteFile(file->handle, itr, mem_end(data) - itr, &bytesWritten, null)) {
      itr += bytesWritten;
      continue;
    }
    return file_result_from_lasterror();
  }
  return File_Success;
}
