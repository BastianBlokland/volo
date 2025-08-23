#include "core/alloc.h"
#include "core/bits.h"
#include "core/diag.h"
#include "core/dynstring.h"
#include "core/file.h"
#include "core/math.h"

#include "file.h"
#include "time.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * For compatiblity with older GLIBC versions we use the older '__xstat' and '__fxstat'.
 * TODO: It might be better to use the syscalls directly.
 */
int SYS_DECL __xstat(int ver, const char* fileName, struct stat* statBuf);
int SYS_DECL __fxstat(int ver, int fileDes, struct stat* statBuf);

File* g_fileStdIn  = &(File){.handle = 0, .access = FileAccess_Read};
File* g_fileStdOut = &(File){.handle = 1, .access = FileAccess_Write};
File* g_fileStdErr = &(File){.handle = 2, .access = FileAccess_Write};

static usize g_filePageSize;

NO_INLINE_HINT static FileResult fileresult_from_errno(void) {
  switch (errno) {
  case EACCES:
  case EPERM:
  case EROFS:
    return FileResult_NoAccess;
  case EFBIG:
    return FileResult_FileTooBig;
  case ETXTBSY:
    return FileResult_Locked;
  case EDQUOT:
  case ENOSPC:
    return FileResult_DiskFull;
  case ENOENT:
    return FileResult_NotFound;
  case EMFILE:
  case ENFILE:
    return FileResult_TooManyOpenFiles;
  case ENAMETOOLONG:
    return FileResult_PathTooLong;
  case EEXIST:
    return FileResult_AlreadyExists;
  case EINVAL:
    return FileResult_InvalidFilename;
  case EISDIR:
    return FileResult_IsDirectory;
  }
  return FileResult_UnknownError;
}

static FileInfo fileinfo_from_stat(const struct stat* stat) {
  FileType fileType = FileType_Unknown;
  if (S_ISREG(stat->st_mode)) {
    fileType = FileType_Regular;
  } else if (S_ISDIR(stat->st_mode)) {
    fileType = FileType_Directory;
  }
  return (FileInfo){
      .size       = stat->st_size,
      .type       = fileType,
      .accessTime = time_pal_native_to_real(stat->st_atim),
      .modTime    = time_pal_native_to_real(stat->st_mtim),
  };
}

static void file_pal_close(const int fd) {
TryClose:
  if (UNLIKELY(close(fd) < 0)) {
    switch (errno) {
    case EINTR:
      goto TryClose; // Interrupted; retry.
    default:
      diag_crash_msg("Failed to close file-descriptor: {}", fmt_int(fd));
    }
  }
}

void file_pal_init(void) {
  g_filePageSize = getpagesize();
  if (UNLIKELY(!bits_ispow2(g_filePageSize))) {
    diag_crash_msg("Non pow2 page-size is not supported");
  }
}

bool file_std_unused(void) {
  // TODO: Detect if the parent has closed their end of the std pipes.
  return false;
}

FileResult file_std_close(void) {
  FileResult result = FileResult_Success;
  if (g_fileStdIn) {
    file_pal_close(g_fileStdIn->handle);
  }
  if (g_fileStdOut) {
    file_pal_close(g_fileStdOut->handle);
  }
  if (g_fileStdErr) {
    file_pal_close(g_fileStdErr->handle);
  }
  g_fileStdIn = g_fileStdOut = g_fileStdErr = null;
  return result;
}

FileResult file_pal_create(
    Allocator*            alloc,
    const String          path,
    const FileMode        mode,
    const FileAccessFlags access,
    File**                file) {
  // Copy the path on the stack and null-terminate it.
  if (path.size >= PATH_MAX) {
    return FileResult_PathTooLong;
  }
  Mem pathBuffer = mem_stack(PATH_MAX);
  mem_cpy(pathBuffer, path);
  *mem_at_u8(pathBuffer, path.size) = '\0';

  int flags = O_NOCTTY;

  switch (mode) {
  case FileMode_Open:
    break;
  case FileMode_Append:
    flags |= O_CREAT | O_APPEND;
    break;
  case FileMode_Create:
    flags |= O_CREAT | O_TRUNC;
    break;
  default:
    diag_assert_fail("Invalid FileMode: {}", fmt_int(flags));
  }

  if ((access & FileAccess_Read) && (access & FileAccess_Write)) {
    flags |= O_RDWR;
  } else if (access & FileAccess_Read) {
    flags |= O_RDONLY;
  } else if (access & FileAccess_Write) {
    flags |= O_WRONLY;
  }

  const int newFilePerms = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // RW for owner, and R for others.
  const int fd           = open(pathBuffer.ptr, flags, newFilePerms);
  if (fd < 0) {
    return fileresult_from_errno();
  }

  *file  = alloc_alloc_t(alloc, File);
  **file = (File){
      .handle   = fd,
      .access   = access,
      .alloc    = alloc,
      .mappings = dynarray_create_t(alloc, FileMapping, 0),
  };
  return FileResult_Success;
}

FileResult file_pal_temp(Allocator* alloc, File** file) {
  // Create a null terminated string on the stack that will be modifed by mkstemp to contain the
  // unique name.
  String nameTemplate = string_lit("volo_tmp_XXXXXX");
  Mem    nameBuffer   = mem_stack(PATH_MAX);
  mem_cpy(nameBuffer, nameTemplate);
  *mem_at_u8(nameBuffer, nameTemplate.size) = '\0'; // Null-terminate.

  int fd = mkstemp(nameBuffer.ptr);
  if (fd < 0) {
    return fileresult_from_errno();
  }

  unlink(nameBuffer.ptr); // Immediately unlink the file, so it will be deleted on close.

  *file  = alloc_alloc_t(alloc, File);
  **file = (File){
      .handle   = fd,
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
  file_pal_close(file->handle);
  alloc_free_t(file->alloc, file);
}

FileResult file_write_sync(File* file, const String data) {
  diag_assert(file);
  diag_assert_msg(file->access & FileAccess_Write, "File handle does not have write access");

  for (u8* itr = mem_begin(data); itr != mem_end(data);) {
    const ssize_t res = write(file->handle, itr, mem_end(data) - itr);
    if (res > 0) {
      itr += res;
      continue;
    }
    switch (errno) {
    case EAGAIN:
    case EINTR:
      continue; // Retry on interrupt.
    }
    return fileresult_from_errno();
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

  Mem readBuffer = mem_stack(usize_kibibyte * 16);
  while (true) {
    const ssize_t res = read(file->handle, readBuffer.ptr, readBuffer.size);
    if (res > 0) {
      dynstring_append(dynstr, mem_slice(readBuffer, 0, res));
      return FileResult_Success;
    }
    if (res == 0) {
      return FileResult_NoDataAvailable;
    }
    switch (errno) {
    case EINTR:
      continue; // Retry on interrupt.
    }
    return fileresult_from_errno();
  }
}

FileResult file_crc_32_sync(File* file, u32* outCrc32) {
  diag_assert(file);
  diag_assert_msg(file->access & FileAccess_Read, "File handle does not have read access");

  *outCrc32 = 0;

  Mem readBuffer = mem_stack(usize_kibibyte * 16);
  for (;;) {
    const ssize_t res = read(file->handle, readBuffer.ptr, readBuffer.size);
    if (res > 0) {
      *outCrc32 = bits_crc_32(*outCrc32, mem_slice(readBuffer, 0, (usize)res));
      continue;
    }
    if (res == 0) {
      return FileResult_Success;
    }
    switch (errno) {
    case EINTR:
      continue; // Retry on interrupt.
    }
    return fileresult_from_errno();
  }
}

FileResult file_skip_sync(File* file, usize bytes) {
  diag_assert(file);
  diag_assert_msg(file->access & FileAccess_Read, "File handle does not have read access");

  Mem readBuffer = mem_stack(usize_kibibyte * 16);
  while (bytes) {
    const ssize_t res = read(file->handle, readBuffer.ptr, math_min(readBuffer.size, bytes));
    if (res > 0) {
      bytes -= res;
      continue;
    }
    if (res == 0) {
      return FileResult_NoDataAvailable;
    }
    switch (errno) {
    case EINTR:
      continue; // Retry on interrupt.
    }
    return fileresult_from_errno();
  }

  return FileResult_Success;
}

FileResult file_position_sync(File* file, usize* outPosition) {
  diag_assert(file);

  const off_t result = lseek(file->handle, 0, SEEK_CUR);
  if (UNLIKELY(result < 0)) {
    return fileresult_from_errno();
  }
  *outPosition = (usize)result;
  return FileResult_Success;
}

FileResult file_seek_sync(File* file, const usize position) {
  diag_assert(file);

  if (UNLIKELY(lseek(file->handle, position, SEEK_SET) < 0)) {
    return fileresult_from_errno();
  }
  return FileResult_Success;
}

FileResult file_resize_sync(File* file, const usize size) {
  diag_assert(file);
  diag_assert_msg(file->access & FileAccess_Write, "File handle does not have write access");

  if (UNLIKELY(ftruncate(file->handle, size) < 0)) {
    return fileresult_from_errno();
  }
  if (UNLIKELY(lseek(file->handle, size, SEEK_SET) < 0)) {
    return fileresult_from_errno();
  }
  return FileResult_Success;
}

FileInfo file_stat_sync(File* file) {
  diag_assert(file);

  struct stat statOutput;
  // NOTE: Use '__fxstat' instead of 'fstat' for compatiblity with older GLIBC versions.
  const int res = __fxstat(1 /* _STAT_VER */, file->handle, &statOutput);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("fstat() failed: {}", fmt_int(res));
  }
  return fileinfo_from_stat(&statOutput);
}

FileInfo file_stat_path_sync(const String path) {
  // Copy the path on the stack and null-terminate it.
  if (path.size >= PATH_MAX) {
    return (FileInfo){0};
  }
  Mem pathBuffer = mem_stack(PATH_MAX);
  mem_cpy(pathBuffer, path);
  *mem_at_u8(pathBuffer, path.size) = '\0';

  struct stat statOutput;
  // NOTE: Use '__xstat' instead of 'stat' for compatiblity with older GLIBC versions.
  const int res = __xstat(1 /* _STAT_VER */, pathBuffer.ptr, &statOutput);
  if (res != 0) {
    switch (errno) {
    case EACCES:
    case ELOOP:
    case ENOENT:
      return (FileInfo){0};
    default:
      diag_crash_msg("stat() failed: {} (errno: {})", fmt_int(res), fmt_int(errno));
    }
  }
  return fileinfo_from_stat(&statOutput);
}

FileResult file_delete_sync(const String path) {
  // Copy the path on the stack and null-terminate it.
  if (path.size >= PATH_MAX) {
    return FileResult_PathTooLong;
  }
  Mem pathBuffer = mem_stack(PATH_MAX);
  mem_cpy(pathBuffer, path);
  *mem_at_u8(pathBuffer, path.size) = '\0';

  if (unlink((const char*)pathBuffer.ptr)) {
    return fileresult_from_errno();
  }
  return FileResult_Success;
}

FileResult file_delete_dir_sync(const String path) {
  // Copy the path on the stack and null-terminate it.
  if (path.size >= PATH_MAX) {
    return FileResult_PathTooLong;
  }
  Mem pathBuffer = mem_stack(PATH_MAX);
  mem_cpy(pathBuffer, path);
  *mem_at_u8(pathBuffer, path.size) = '\0';

  if (rmdir((const char*)pathBuffer.ptr)) {
    return fileresult_from_errno();
  }
  return FileResult_Success;
}

FileResult
file_pal_map(File* file, const usize offset, usize size, const FileHints hints, FileMapping* out) {
  diag_assert_msg(file->access != 0, "File handle does not have read or write access");

  const usize offsetAligned = offset / g_filePageSize * g_filePageSize;
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

  int prot = 0;
  if (file->access & FileAccess_Read) {
    prot |= PROT_READ;
  }
  if (file->access & FileAccess_Write) {
    prot |= PROT_WRITE;
  }
  void* addr = mmap(null, size + padding, prot, MAP_SHARED, file->handle, offsetAligned);
  if (UNLIKELY(!addr)) {
    return errno == EINVAL ? FileResult_InvalidMapping : fileresult_from_errno();
  }

  if (hints & FileHints_Prefetch) {
    const int advice = POSIX_FADV_WILLNEED;
    if (UNLIKELY(posix_fadvise(file->handle, offsetAligned, size + padding, advice) != 0)) {
      diag_crash_msg("posix_fadvise() (errno: {})", fmt_int(errno));
    }
  }

  *out = (FileMapping){
      .offset = offset,
      .ptr    = bits_ptr_offset(addr, padding),
      .size   = size,
  };
  return FileResult_Success;
}

FileResult file_pal_unmap(File* file, FileMapping* mapping) {
  (void)file;
  diag_assert_msg(mapping->ptr, "Invalid mapping");

  const usize offsetAligned = mapping->offset / g_filePageSize * g_filePageSize;
  const usize padding       = mapping->offset - offsetAligned;
  void*       alignedPtr    = bits_ptr_offset(mapping->ptr, -(iptr)padding);

  const int res = munmap(alignedPtr, mapping->size + padding);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("munmap() failed: {} (errno: {})", fmt_int(res), fmt_int(errno));
  }

  return FileResult_Success;
}

FileResult file_rename(const String oldPath, const String newPath) {
  // Copy the paths on the stack and null-terminate them.
  if (oldPath.size >= PATH_MAX || newPath.size >= PATH_MAX) {
    return FileResult_PathTooLong;
  }

  Mem oldPathBuffer = mem_stack(PATH_MAX);
  mem_cpy(oldPathBuffer, oldPath);
  *mem_at_u8(oldPathBuffer, oldPath.size) = '\0';

  Mem newPathBuffer = mem_stack(PATH_MAX);
  mem_cpy(newPathBuffer, newPath);
  *mem_at_u8(newPathBuffer, newPath.size) = '\0';

  const int res = rename((const char*)oldPathBuffer.ptr, (const char*)newPathBuffer.ptr);
  return res != 0 ? fileresult_from_errno() : FileResult_Success;
}

FileResult file_pal_create_dir_single_sync(const String path) {
  // Copy the path on the stack and null-terminate it.
  if (path.size >= PATH_MAX) {
    return FileResult_PathTooLong;
  }
  Mem pathBuffer = mem_stack(PATH_MAX);
  mem_cpy(pathBuffer, path);
  *mem_at_u8(pathBuffer, path.size) = '\0';

  // RWX for owner and group, RX for others.
  const int perms = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IXOTH;
  const int res   = mkdir((const char*)pathBuffer.ptr, perms);
  return res != 0 ? fileresult_from_errno() : FileResult_Success;
}
