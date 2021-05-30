#include "core_diag.h"
#include "core_file.h"
#include "core_sentinel.h"
#include "core_winutils.h"
#include "file_internal.h"
#include <Windows.h>

File* g_file_stdin;
File* g_file_stdout;
File* g_file_stderr;

void file_pal_init() {
  static File stdIn = {};
  stdIn.handle      = GetStdHandle(STD_INPUT_HANDLE);
  if (stdIn.handle != INVALID_HANDLE_VALUE) {
    g_file_stdin = &stdIn;
  }

  static File stdOut = {};
  stdOut.handle      = GetStdHandle(STD_OUTPUT_HANDLE);
  if (stdOut.handle != INVALID_HANDLE_VALUE) {
    g_file_stdout = &stdOut;
  }

  static File stdErr = {};
  stdErr.handle      = GetStdHandle(STD_ERROR_HANDLE);
  if (stdErr.handle != INVALID_HANDLE_VALUE) {
    g_file_stderr = &stdErr;
  }
}

static FileResult fileresult_from_lasterror() {
  switch (GetLastError()) {
  case ERROR_ACCESS_DENIED:
    return FileResult_NoAccess;
  case ERROR_SHARING_VIOLATION:
    return FileResult_Locked;
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
  case ERROR_INVALID_DRIVE:
    return FileResult_NotFound;
  case ERROR_DISK_FULL:
    return FileResult_DiskFull;
  case ERROR_TOO_MANY_OPEN_FILES:
    return FileResult_TooManyOpenFiles;
  case ERROR_BUFFER_OVERFLOW:
    return FileResult_PathTooLong;
  case ERROR_INVALID_NAME:
  case ERROR_BAD_PATHNAME:
    return FileResult_InvalidFilename;
  case ERROR_FILE_EXISTS:
  case ERROR_ALREADY_EXISTS:
    return FileResult_AlreadyExists;
  }
  return FileResult_UnknownError;
}

FileResult
file_create(Allocator* alloc, String path, FileMode mode, FileAccessFlags access, File** file) {
  // Convert the path to a null-terminated wide-char string.
  usize pathBufferSize = winutils_to_widestr_size(path);
  if (sentinel_check(pathBufferSize)) {
    return FileResult_PathInvalid;
  }
  Mem pathBufferMem = alloc_alloc(g_allocator_heap, pathBufferSize);
  winutils_to_widestr(pathBufferMem, path);

  DWORD shareMode =
      FILE_SHARE_READ | FILE_SHARE_WRITE; // Consider a flag for specifying no concurrent writes?
  DWORD desiredAccess       = 0;
  DWORD creationDisposition = 0;
  DWORD flags               = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_POSIX_SEMANTICS;

  switch (mode) {
  case FileMode_Open:
    creationDisposition = OPEN_EXISTING;
    break;
  case FileMode_Append:
    desiredAccess |= FILE_APPEND_DATA;
    creationDisposition = OPEN_ALWAYS;
    break;
  case FileMode_Create:
    creationDisposition = CREATE_ALWAYS;
    break;
  default:
    diag_assert_msg(false, "Invalid FileMode");
  }

  if (access & FileAccess_Read) {
    desiredAccess |= GENERIC_READ;
  }
  if (mode != FileMode_Append && access & FileAccess_Write) {
    desiredAccess |= GENERIC_WRITE;
  }

  HANDLE handle = CreateFileW(
      (const wchar_t*)pathBufferMem.ptr,
      desiredAccess,
      shareMode,
      null,
      creationDisposition,
      flags,
      null);

  alloc_free(g_allocator_heap, pathBufferMem);

  if (handle == INVALID_HANDLE_VALUE) {
    return fileresult_from_lasterror();
  }
  Mem allocation = alloc_alloc(alloc, sizeof(File));
  *file          = mem_as_t(allocation, File);
  **file         = (File){
      .handle     = handle,
      .alloc      = alloc,
      .allocation = allocation,
  };
  return FileResult_Success;
}

void file_destroy(File* file) {
  diag_assert_msg(file->alloc, "Invalid file");
  CloseHandle(file->handle);
  alloc_free(file->alloc, file->allocation);
}

FileResult file_write_sync(File* file, const String data) {
  diag_assert(file);

  for (u8* itr = mem_begin(data); itr != mem_end(data);) {
    DWORD bytesWritten;
    if (WriteFile(file->handle, itr, mem_end(data) - itr, &bytesWritten, null)) {
      itr += bytesWritten;
      continue;
    }
    return fileresult_from_lasterror();
  }
  return FileResult_Success;
}

FileResult file_read_sync(File* file, DynString* dynstr) {
  diag_assert(file);

  Mem   readBuffer = mem_stack(1024);
  DWORD bytesRead;
  BOOL  success = ReadFile(file->handle, readBuffer.ptr, readBuffer.size, &bytesRead, null);
  if (success && bytesRead) {
    dynstring_append(dynstr, mem_slice(readBuffer, 0, bytesRead));
    return FileResult_Success;
  }
  if (success) {
    return FileResult_NoDataAvailable;
  }
  return fileresult_from_lasterror();
}

FileResult file_delete_sync(String path) {
  // Convert the path to a null-terminated wide-char string.
  usize pathBufferSize = winutils_to_widestr_size(path);
  if (sentinel_check(pathBufferSize)) {
    return FileResult_PathInvalid;
  }
  Mem pathBufferMem = alloc_alloc(g_allocator_heap, pathBufferSize);
  winutils_to_widestr(pathBufferMem, path);

  BOOL success = DeleteFileW(pathBufferMem.ptr);

  alloc_free(g_allocator_heap, pathBufferMem);

  return success ? FileResult_Success : fileresult_from_lasterror();
}
