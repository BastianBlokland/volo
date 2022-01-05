#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_sentinel.h"
#include "core_winutils.h"

#include "file_internal.h"
#include "path_internal.h"
#include "time_internal.h"

#include <Windows.h>

typedef struct {
  HANDLE mappingObj;
  void*  addr;
} FileMapping;

File* g_file_stdin;
File* g_file_stdout;
File* g_file_stderr;

void file_pal_init() {
  static File stdIn = {0};
  stdIn.handle      = GetStdHandle(STD_INPUT_HANDLE);
  stdIn.access      = FileAccess_Read;
  if (stdIn.handle != INVALID_HANDLE_VALUE) {
    g_file_stdin = &stdIn;
  }

  static File stdOut = {0};
  stdOut.handle      = GetStdHandle(STD_OUTPUT_HANDLE);
  stdOut.access      = FileAccess_Write;
  if (stdOut.handle != INVALID_HANDLE_VALUE) {
    g_file_stdout = &stdOut;
  }

  static File stdErr = {0};
  stdErr.handle      = GetStdHandle(STD_ERROR_HANDLE);
  stdErr.access      = FileAccess_Write;
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
  const usize pathBufferSize = winutils_to_widestr_size(path);
  if (sentinel_check(pathBufferSize)) {
    return FileResult_PathInvalid;
  }
  if (pathBufferSize > path_pal_max_size) {
    return FileResult_PathTooLong;
  }
  Mem pathBufferMem = mem_stack(pathBufferSize);
  winutils_to_widestr(pathBufferMem, path);

  DWORD shareMode =
      access == 0 ? (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE) : FILE_SHARE_READ;
  DWORD desiredAccess       = 0;
  DWORD creationDisposition = 0;
  DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_POSIX_SEMANTICS;

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
    diag_assert_fail("Invalid FileMode: {}", fmt_int(mode));
  }

  if (access & FileAccess_Read) {
    desiredAccess |= GENERIC_READ;
  }
  if (mode != FileMode_Append && access & FileAccess_Write) {
    desiredAccess |= GENERIC_WRITE;
  }

  HANDLE handle = CreateFile(
      (const wchar_t*)pathBufferMem.ptr,
      desiredAccess,
      shareMode,
      null,
      creationDisposition,
      flags,
      null);

  if (handle == INVALID_HANDLE_VALUE) {
    return fileresult_from_lasterror();
  }

  *file  = alloc_alloc_t(alloc, File);
  **file = (File){
      .handle = handle,
      .access = access,
      .alloc  = alloc,
  };
  return FileResult_Success;
}

FileResult file_temp(Allocator* alloc, File** file) {
  // Use 'GetTempPath' and 'GetTempFileName' to generate a unique filename in a temporary directory.
  Mem         tempDirPath  = mem_stack((MAX_PATH + 1) * sizeof(wchar_t)); // +1 for null-terminator.
  const DWORD tempDirChars = GetTempPath(MAX_PATH, (wchar_t*)tempDirPath.ptr);
  if (!tempDirChars) {
    return fileresult_from_lasterror();
  }

  Mem tempFilePath = mem_stack(MAX_PATH * sizeof(wchar_t));
  if (GetTempFileName(
          (const wchar_t*)tempDirPath.ptr, TEXT("vol"), 0, (wchar_t*)tempFilePath.ptr) == 0) {
    return fileresult_from_lasterror();
  }

  HANDLE handle = CreateFile(
      (const wchar_t*)tempFilePath.ptr,
      GENERIC_READ | GENERIC_WRITE,
      0,
      null,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
      null);

  if (handle == INVALID_HANDLE_VALUE) {
    return fileresult_from_lasterror();
  }

  *file  = alloc_alloc_t(alloc, File);
  **file = (File){
      .handle = handle,
      .access = FileAccess_Read | FileAccess_Write,
      .alloc  = alloc,
  };
  return FileResult_Success;
}

void file_destroy(File* file) {
  diag_assert_msg(file->alloc, "Invalid file");

  if (file->mapping) {
    FileMapping* mapping = file->mapping;
    const bool   success = UnmapViewOfFile(mapping->addr) && CloseHandle(mapping->mappingObj);
    if (UNLIKELY(!success)) {
      diag_crash_msg("UnmapViewOfFile() or CloseHandle() failed");
    }
    alloc_free_t(file->alloc, mapping);
  }

  CloseHandle(file->handle);
  alloc_free_t(file->alloc, file);
}

FileResult file_write_sync(File* file, const String data) {
  diag_assert(file);
  diag_assert_msg(file->access & FileAccess_Write, "File handle does not have write access");

  for (u8* itr = mem_begin(data); itr != mem_end(data);) {
    DWORD bytesWritten;
    if (WriteFile(file->handle, itr, (DWORD)(mem_end(data) - itr), &bytesWritten, null)) {
      itr += bytesWritten;
      continue;
    }
    return fileresult_from_lasterror();
  }
  return FileResult_Success;
}

FileResult file_read_sync(File* file, DynString* dynstr) {
  diag_assert(file);
  diag_assert_msg(file->access & FileAccess_Read, "File handle does not have read access");

  /**
   * TODO: Consider reserving space in the output DynString and directly reading into that to avoid
   * the copy. Downside is for small reads we would grow the DynString unnecessarily.
   */

  Mem   readBuffer = mem_stack(usize_kibibyte);
  DWORD bytesRead;
  BOOL  success = ReadFile(file->handle, readBuffer.ptr, (DWORD)readBuffer.size, &bytesRead, null);
  if (success && bytesRead) {
    dynstring_append(dynstr, mem_slice(readBuffer, 0, bytesRead));
    return FileResult_Success;
  }
  if (success) {
    return FileResult_NoDataAvailable;
  }
  return fileresult_from_lasterror();
}

FileResult file_seek_sync(File* file, usize position) {
  LARGE_INTEGER li;
  li.QuadPart = position;
  li.LowPart  = SetFilePointer(file->handle, li.LowPart, &li.HighPart, FILE_BEGIN);
  if (li.LowPart == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
    return fileresult_from_lasterror();
  }
  return FileResult_Success;
}

FileInfo file_stat_sync(File* file) {
  BY_HANDLE_FILE_INFORMATION info;
  const BOOL                 success = GetFileInformationByHandle(file->handle, &info);
  if (UNLIKELY(!success)) {
    diag_crash_msg("GetFileInformationByHandle() failed");
  }

  const FileType fileType =
      (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? FileType_Directory : FileType_Regular;

  LARGE_INTEGER fileSize;
  fileSize.LowPart  = info.nFileSizeLow;
  fileSize.HighPart = info.nFileSizeHigh;
  return (FileInfo){
      .size       = (usize)fileSize.QuadPart,
      .type       = fileType,
      .accessTime = time_pal_native_to_real(&info.ftLastAccessTime),
      .modTime    = time_pal_native_to_real(&info.ftLastWriteTime),
  };
}

FileResult file_delete_sync(String path) {
  // Convert the path to a null-terminated wide-char string.
  const usize pathBufferSize = winutils_to_widestr_size(path);
  if (sentinel_check(pathBufferSize)) {
    return FileResult_PathInvalid;
  }
  if (pathBufferSize > path_pal_max_size) {
    return FileResult_PathTooLong;
  }
  Mem pathBufferMem = mem_stack(pathBufferSize);
  winutils_to_widestr(pathBufferMem, path);

  const BOOL success = DeleteFile(pathBufferMem.ptr);
  return success ? FileResult_Success : fileresult_from_lasterror();
}

FileResult file_delete_dir_sync(String path) {
  // Convert the path to a null-terminated wide-char string.
  const usize pathBufferSize = winutils_to_widestr_size(path);
  if (sentinel_check(pathBufferSize)) {
    return FileResult_PathInvalid;
  }
  if (pathBufferSize > path_pal_max_size) {
    return FileResult_PathTooLong;
  }
  Mem pathBufferMem = mem_stack(pathBufferSize);
  winutils_to_widestr(pathBufferMem, path);

  const BOOL success = RemoveDirectory(pathBufferMem.ptr);
  return success ? FileResult_Success : fileresult_from_lasterror();
}

FileResult file_map(File* file, String* output) {
  diag_assert_msg(!file->mapping, "File is already mapped");

  LARGE_INTEGER size;
  size.QuadPart = file_stat_sync(file).size;

  const DWORD  protect = (file->access & FileAccess_Write) ? PAGE_READWRITE : PAGE_READONLY;
  const HANDLE mappingObj =
      CreateFileMapping(file->handle, null, protect, size.HighPart, size.LowPart, null);
  if (UNLIKELY(!mappingObj)) {
    return fileresult_from_lasterror();
  }

  const DWORD access = (file->access & FileAccess_Write) ? FILE_MAP_WRITE : FILE_MAP_READ;
  void*       addr   = MapViewOfFile(mappingObj, access, 0, 0, size.QuadPart);
  if (UNLIKELY(!addr)) {
    const bool success = CloseHandle(mappingObj);
    if (UNLIKELY(!success)) {
      diag_crash_msg("CloseHandle() failed");
    }
    return fileresult_from_lasterror();
  }

  file->mapping = alloc_alloc_t(file->alloc, FileMapping);
  if (UNLIKELY(!file->mapping)) {
    const bool success = UnmapViewOfFile(addr) && CloseHandle(mappingObj);
    if (UNLIKELY(!success)) {
      diag_crash_msg("UnmapViewOfFile() or CloseHandle() failed");
    }
    return FileResult_AllocationFailed;
  }

  *(FileMapping*)file->mapping = (FileMapping){.mappingObj = mappingObj, .addr = addr};
  *output                      = mem_create(addr, (usize)size.QuadPart);
  return FileResult_Success;
}

FileResult file_pal_create_dir_single_sync(String path) {
  // Convert the path to a null-terminated wide-char string.
  const usize pathBufferSize = winutils_to_widestr_size(path);
  if (sentinel_check(pathBufferSize)) {
    return FileResult_PathInvalid;
  }
  if (pathBufferSize > path_pal_max_size) {
    return FileResult_PathTooLong;
  }
  Mem pathBufferMem = mem_stack(pathBufferSize);
  winutils_to_widestr(pathBufferMem, path);

  const BOOL success = CreateDirectory((const wchar_t*)pathBufferMem.ptr, null);
  return success ? FileResult_Success : fileresult_from_lasterror();
}
