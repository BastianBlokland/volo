#include "core/alloc.h"
#include "core/array.h"
#include "core/bits.h"
#include "core/diag.h"
#include "core/dynstring.h"
#include "core/file.h"
#include "core/math.h"
#include "core/sentinel.h"
#include "core/winutils.h"

#include "file.h"
#include "path.h"
#include "time.h"

#include <Windows.h>

File* g_fileStdIn;
File* g_fileStdOut;
File* g_fileStdErr;

static usize g_fileAllocGranularity;

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

  SYSTEM_INFO si;
  GetSystemInfo(&si);
  g_fileAllocGranularity = si.dwAllocationGranularity;
  if (UNLIKELY(!bits_ispow2(g_fileAllocGranularity))) {
    diag_crash_msg("Non pow2 file allocation granularity is not supported");
  }
}

NO_INLINE_HINT static FileResult fileresult_from_lasterror(void) {
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
  case ERROR_MAPPED_ALIGNMENT:
    return FileResult_InvalidMapping;
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

bool file_std_unused(void) {
  if (g_fileStdIn && GetFileType(g_fileStdIn->handle) != FILE_TYPE_CHAR) {
    return false; // Attached to a pipe. TODO: Detect if the parent has closed their end.
  }
  if (g_fileStdOut && GetFileType(g_fileStdOut->handle) != FILE_TYPE_CHAR) {
    return false; // Attached to a pipe. TODO: Detect if the parent has closed their end.
  }
  if (g_fileStdErr && GetFileType(g_fileStdErr->handle) != FILE_TYPE_CHAR) {
    return false; // Attached to a pipe. TODO: Detect if the parent has closed their end.
  }
  DWORD       pids[2];
  const DWORD numPids = GetConsoleProcessList(pids, array_elems(pids));
  if (numPids > 1) {
    return false; // Multiple processes are attached to our console.
  }
  return true; // No other processes are reading our std handles.
}

FileResult file_std_close(void) {
  FileResult result = FileResult_Success;
  if (g_fileStdIn) {
    CloseHandle(g_fileStdIn->handle);
  }
  if (g_fileStdOut) {
    CloseHandle(g_fileStdOut->handle);
  }
  if (g_fileStdErr) {
    CloseHandle(g_fileStdErr->handle);
  }
  g_fileStdIn = g_fileStdOut = g_fileStdErr = null;

  if (!FreeConsole()) {
    diag_crash_msg("FreeConsole() failed");
  }
  return result;
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
      .handle   = handle,
      .access   = access,
      .alloc    = alloc,
      .mappings = dynarray_create_t(alloc, FileMapping, 0),
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
      .handle   = handle,
      .access   = FileAccess_Read | FileAccess_Write,
      .alloc    = alloc,
      .mappings = dynarray_create_t(alloc, FileMapping, 0),
  };
  return FileResult_Success;
}

void file_pal_destroy(File* file) {
  diag_assert_msg(file->alloc, "Invalid file");
  diag_assert_msg(!file->mappings.size, "Mappings left open");

  if (file->mappings.stride) {
    dynarray_destroy(&file->mappings);
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

FileResult file_crc_32_sync(File* file, u32* outCrc32) {
  diag_assert(file);
  diag_assert_msg(file->access & FileAccess_Read, "File handle does not have read access");

  *outCrc32 = 0;

  Mem readBuffer = mem_stack(usize_kibibyte * 16);
  for (;;) {
    const DWORD bytesToRead = (DWORD)readBuffer.size;
    DWORD       bytesRead;
    const bool  success = ReadFile(file->handle, readBuffer.ptr, bytesToRead, &bytesRead, null);
    if (success && bytesRead) {
      *outCrc32 = bits_crc_32(*outCrc32, mem_slice(readBuffer, 0, (usize)bytesRead));
      continue;
    }
    if (success) {
      return FileResult_Success;
    }
    return fileresult_from_lasterror();
  }
}

FileResult file_skip_sync(File* file, usize bytes) {
  diag_assert(file);
  diag_assert_msg(file->access & FileAccess_Read, "File handle does not have read access");

  Mem readBuffer = mem_stack(usize_kibibyte * 16);
  while (bytes) {
    const DWORD bytesToRead = (DWORD)math_min(readBuffer.size, bytes);
    DWORD       bytesRead;
    const bool  success = ReadFile(file->handle, readBuffer.ptr, bytesToRead, &bytesRead, null);
    if (success && bytesRead) {
      bytes -= bytesRead;
      continue;
    }
    if (success || GetLastError() == ERROR_BROKEN_PIPE) {
      return FileResult_NoDataAvailable;
    }
    return fileresult_from_lasterror();
  }

  return FileResult_Success;
}

FileResult file_position_sync(File* file, usize* outPosition) {
  diag_assert(file);

  LARGE_INTEGER pos;
  if (UNLIKELY(!SetFilePointerEx(file->handle, (LARGE_INTEGER){0}, &pos, FILE_CURRENT))) {
    return fileresult_from_lasterror();
  }

  *outPosition = (usize)pos.QuadPart;
  return FileResult_Success;
}

FileResult file_seek_sync(File* file, const usize position) {
  diag_assert(file);

  LARGE_INTEGER li;
  li.QuadPart = position;
  li.LowPart  = SetFilePointer(file->handle, li.LowPart, &li.HighPart, FILE_BEGIN);
  if (UNLIKELY(li.LowPart == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)) {
    return fileresult_from_lasterror();
  }
  return FileResult_Success;
}

FileResult file_resize_sync(File* file, const usize size) {
  diag_assert(file);
  diag_assert_msg(file->access & FileAccess_Write, "File handle does not have write access");

  LARGE_INTEGER endPos;
  endPos.QuadPart = size;
  if (UNLIKELY(!SetFilePointerEx(file->handle, endPos, null, FILE_BEGIN))) {
    return fileresult_from_lasterror();
  }
  if (UNLIKELY(!SetEndOfFile(file->handle))) {
    return fileresult_from_lasterror();
  }
  return FileResult_Success;
}

FileInfo file_stat_sync(File* file) {
  diag_assert(file);

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

FileResult
file_pal_map(File* file, const usize offset, usize size, const FileHints hints, FileMapping* out) {
  diag_assert_msg(file->access != 0, "File handle does not have read or write access");

  const usize offsetAligned = offset / g_fileAllocGranularity * g_fileAllocGranularity;
  const usize padding       = offset - offsetAligned;

  if (!size) {
    size = file_stat_sync(file).size;
    if (UNLIKELY(offset > size)) {
      return FileResult_InvalidMapping;
    }
    size -= offset;
  }
  if (UNLIKELY(!size)) {
    return FileResult_FileEmpty;
  }

  const DWORD  protect = (file->access & FileAccess_Write) ? PAGE_READWRITE : PAGE_READONLY;
  const HANDLE mapObj  = CreateFileMapping(file->handle, null, protect, 0, 0, null);
  if (UNLIKELY(!mapObj)) {
    return fileresult_from_lasterror();
  }

  const LARGE_INTEGER offsetReq = {.QuadPart = offsetAligned};
  const usize         sizeReq   = size + padding;
  const DWORD         access = (file->access & FileAccess_Write) ? FILE_MAP_WRITE : FILE_MAP_READ;

  void* addr = MapViewOfFile(mapObj, access, offsetReq.HighPart, offsetReq.LowPart, sizeReq);
  if (UNLIKELY(!addr)) {
    if (UNLIKELY(!CloseHandle(mapObj))) {
      diag_crash_msg("CloseHandle() failed");
    }
    return fileresult_from_lasterror();
  }

  if (hints & FileHints_Prefetch) {
    WIN32_MEMORY_RANGE_ENTRY entries[] = {
        {.VirtualAddress = addr, .NumberOfBytes = sizeReq},
    };
    HANDLE process = GetCurrentProcess();
    if (UNLIKELY(!PrefetchVirtualMemory(process, array_elems(entries), entries, 0))) {
      diag_crash_msg("PrefetchVirtualMemory() failed");
    }
  }

  *out = (FileMapping){
      .handle = (uptr)mapObj,
      .offset = offset,
      .ptr    = bits_ptr_offset(addr, padding),
      .size   = size,
  };
  return FileResult_Success;
}

FileResult file_pal_unmap(File* file, FileMapping* mapping) {
  (void)file;
  diag_assert_msg(mapping->ptr, "Invalid mapping");

  const usize offsetAligned = mapping->offset / g_fileAllocGranularity * g_fileAllocGranularity;
  const usize padding       = mapping->offset - offsetAligned;
  void*       alignedPtr    = bits_ptr_offset(mapping->ptr, -(iptr)padding);

  const bool success = UnmapViewOfFile(alignedPtr) && CloseHandle((HANDLE)mapping->handle);
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
