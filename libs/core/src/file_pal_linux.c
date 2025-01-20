#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_file.h"

#include "file_internal.h"
#include "time_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

File* g_fileStdIn  = &(File){.handle = 0, .access = FileAccess_Read};
File* g_fileStdOut = &(File){.handle = 1, .access = FileAccess_Write};
File* g_fileStdErr = &(File){.handle = 2, .access = FileAccess_Write};

static FileResult fileresult_from_errno(void) {
  switch (errno) {
  case EACCES:
  case EPERM:
  case EROFS:
    return FileResult_NoAccess;
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

void file_pal_init(void) {}

FileResult
file_pal_create(Allocator* alloc, String path, FileMode mode, FileAccessFlags access, File** file) {
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
      .handle = fd,
      .access = access,
      .alloc  = alloc,
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
      .handle = fd,
      .access = FileAccess_Read | FileAccess_Write,
      .alloc  = alloc,
  };
  return FileResult_Success;
}

void file_pal_destroy(File* file) {
  diag_assert_msg(file->alloc, "Invalid file");

  close(file->handle);
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

FileResult file_seek_sync(File* file, usize position) {
  if (lseek(file->handle, position, SEEK_SET) < 0) {
    return fileresult_from_errno();
  }
  return FileResult_Success;
}

FileInfo file_stat_sync(File* file) {
  struct stat statOutput;
  const int   res = fstat(file->handle, &statOutput);
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
  const int   res = stat(pathBuffer.ptr, &statOutput);
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

FileResult file_delete_sync(String path) {
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

FileResult file_delete_dir_sync(String path) {
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

FileResult file_pal_map(File* file, FileMapping* out, const FileHints hints) {
  diag_assert_msg(file->access != 0, "File handle does not have read or write access");

  const usize size = file_stat_sync(file).size;
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
  void* addr = mmap(null, size, prot, MAP_SHARED, file->handle, 0);
  if (UNLIKELY(!addr)) {
    return fileresult_from_errno();
  }

  if (hints & FileHints_Prefetch) {
    if (UNLIKELY(posix_fadvise(file->handle, 0, size, POSIX_FADV_WILLNEED) != 0)) {
      diag_crash_msg("posix_fadvise() (errno: {})", fmt_int(errno));
    }
  }

  *out = (FileMapping){.ptr = addr, .size = size};
  return FileResult_Success;
}

FileResult file_pal_unmap(File* file, FileMapping* mapping) {
  (void)file;
  diag_assert_msg(mapping->ptr, "Invalid mapping");

  const int res = munmap(mapping->ptr, mapping->size);
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

FileResult file_pal_create_dir_single_sync(String path) {
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
