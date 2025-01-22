#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_file.h"
#include "core_sentinel.h"
#include "core_winutils.h"

#include "file_internal.h"
#include "path_internal.h"
#include "time_internal.h"

#include <Windows.h>

File* g_fileStdIn;
File* g_fileStdOut;
File* g_fileStdErr;

void file_pal_init(void) {
  static File stdIn = {0};
  stdIn.handle      = GetStdHandle(STD_INPUT_HANDLE);
  stdIn.access      = FileAccess_Read;
  if (stdIn.handle != INVALID_HANDLE_VALUE) {
    g_fileStdIn = &stdIn;
  }

  static File stdOut = {0};
  stdOut.handle      = GetStdHandle(STD_OUTPUT_HANDLE);
  stdOut.access      = FileAccess_Write;
  if (stdOut.handle != INVALID_HANDLE_VALUE) {
    g_fileStdOut = &stdOut;
  }

  static File stdErr = {0};
  stdErr.handle      = GetStdHandle(STD_ERROR_HANDLE);
  stdErr.access      = FileAccess_Write;
  if (stdErr.handle != INVALID_HANDLE_VALUE) {
    g_fileStdErr = &stdErr;
  }
}

static FileResult fileresult_from_lasterror(void) {
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

static FileType file_type_from_attributes(const DWORD attributes) {
  if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
    return FileType_Directory;
  }
  if (attributes & FILE_ATTRIBUTE_DEVICE) {
    return FileType_Unknown; // TODO: Should we have a unique type for devices?
  }
  if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
    return FileType_Unknown; // TODO: Should we have a unique type for symlinks?
  }
  return FileType_Regular;
}

FileResult
file_pal_create(Allocator* alloc, String path, FileMode mode, FileAccessFlags access, File** file) {
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

FileResult file_pal_temp(Allocator* alloc, File** file) {
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

void file_pal_destroy(File* file) {
  diag_assert_msg(file->alloc, "Invalid file");

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

  Mem   readBuffer = mem_stack(usize_kibibyte * 16);
  DWORD bytesRead;
  BOOL  success = ReadFile(file->handle, readBuffer.ptr, (DWORD)readBuffer.size, &bytesRead, null);
  if (success && bytesRead) {
    dynstring_append(dynstr, mem_slice(readBuffer, 0, bytesRead));
    return FileResult_Success;
  }
  if (success || GetLastError() == ERROR_BROKEN_PIPE) {
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

  LARGE_INTEGER fileSize;
  fileSize.LowPart  = info.nFileSizeLow;
  fileSize.HighPart = info.nFileSizeHigh;
  return (FileInfo){
      .size       = (usize)fileSize.QuadPart,
      .type       = file_type_from_attributes(info.dwFileAttributes),
      .accessTime = time_pal_native_to_real(&info.ftLastAccessTime),
      .modTime    = time_pal_native_to_real(&info.ftLastWriteTime),
  };
}

FileInfo file_stat_path_sync(const String path) {
  // Convert the path to a null-terminated wide-char string.
  const usize pathBufferSize = winutils_to_widestr_size(path);
  if (sentinel_check(pathBufferSize)) {
    return (FileInfo){0};
  }
  if (pathBufferSize > path_pal_max_size) {
    return (FileInfo){0};
  }
  Mem pathBufferMem = mem_stack(pathBufferSize);
  winutils_to_widestr(pathBufferMem, path);

  WIN32_FILE_ATTRIBUTE_DATA attributeData;
  const BOOL                success =
      GetFileAttributesEx((const wchar_t*)pathBufferMem.ptr, GetFileExInfoStandard, &attributeData);

  if (UNLIKELY(!success)) {
    return (FileInfo){0};
  }

  LARGE_INTEGER fileSize;
  fileSize.LowPart  = attributeData.nFileSizeLow;
  fileSize.HighPart = attributeData.nFileSizeHigh;
  return (FileInfo){
      .size       = (usize)fileSize.QuadPart,
      .type       = file_type_from_attributes(attributeData.dwFileAttributes),
      .accessTime = time_pal_native_to_real(&attributeData.ftLastAccessTime),
      .modTime    = time_pal_native_to_real(&attributeData.ftLastWriteTime),
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

FileResult file_pal_map(File* file, FileMapping* out, const FileHints hints) {
  diag_assert_msg(file->access != 0, "File handle does not have read or write access");

  LARGE_INTEGER size;
  size.QuadPart = file_stat_sync(file).size;
  if (UNLIKELY(!size.QuadPart)) {
    return FileResult_FileEmpty;
  }

  const DWORD  protect = (file->access & FileAccess_Write) ? PAGE_READWRITE : PAGE_READONLY;
  const HANDLE mapObj = CreateFileMapping(file->handle, 0, protect, size.HighPart, size.LowPart, 0);
  if (UNLIKELY(!mapObj)) {
    return fileresult_from_lasterror();
  }

  const DWORD access = (file->access & FileAccess_Write) ? FILE_MAP_WRITE : FILE_MAP_READ;
  void*       addr   = MapViewOfFile(mapObj, access, 0, 0, size.QuadPart);
  if (UNLIKELY(!addr)) {
    const bool success = CloseHandle(mapObj);
    if (UNLIKELY(!success)) {
      diag_crash_msg("CloseHandle() failed");
    }
    return fileresult_from_lasterror();
  }

  if (hints & FileHints_Prefetch) {
    WIN32_MEMORY_RANGE_ENTRY entries[] = {
        {.VirtualAddress = addr, .NumberOfBytes = size.QuadPart},
    };
    HANDLE process = GetCurrentProcess();
    if (UNLIKELY(!PrefetchVirtualMemory(process, array_elems(entries), entries, 0))) {
      diag_crash_msg("PrefetchVirtualMemory() failed");
    }
  }

  *out = (FileMapping){.handle = (uptr)mapObj, .ptr = addr, .size = (usize)size.QuadPart};
  return FileResult_Success;
}

FileResult file_pal_unmap(File* file, FileMapping* mapping) {
  (void)file;
  diag_assert_msg(mapping->ptr, "Invalid mapping");

  const bool success = UnmapViewOfFile(mapping->ptr) && CloseHandle((HANDLE)mapping->handle);
  if (UNLIKELY(!success)) {
    diag_crash_msg("UnmapViewOfFile() or CloseHandle() failed");
  }

  return FileResult_Success;
}

FileResult file_rename(const String oldPath, const String newPath) {
  // Convert the paths to null-terminated wide-char strings.
  const usize oldPathBufferSize = winutils_to_widestr_size(oldPath);
  const usize newPathBufferSize = winutils_to_widestr_size(newPath);
  if (sentinel_check(oldPathBufferSize) || sentinel_check(newPathBufferSize)) {
    return FileResult_PathInvalid;
  }
  if (oldPathBufferSize > path_pal_max_size || newPathBufferSize > path_pal_max_size) {
    return FileResult_PathTooLong;
  }

  Mem oldPathBufferMem = mem_stack(oldPathBufferSize);
  winutils_to_widestr(oldPathBufferMem, oldPath);

  Mem newPathBufferMem = mem_stack(newPathBufferSize);
  winutils_to_widestr(newPathBufferMem, newPath);

  const BOOL success = MoveFileEx(
      (const wchar_t*)oldPathBufferMem.ptr,
      (const wchar_t*)newPathBufferMem.ptr,
      MOVEFILE_REPLACE_EXISTING);
  return success ? FileResult_Success : fileresult_from_lasterror();
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
