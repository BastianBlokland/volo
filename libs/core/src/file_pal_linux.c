#include "core_diag.h"
#include "core_file.h"

#include "linux/limits.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "file_internal.h"

File* g_file_stdin  = &(File){.handle = 0};
File* g_file_stdout = &(File){.handle = 1};
File* g_file_stderr = &(File){.handle = 2};

static FileResult fileresult_from_errno() {
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

void file_pal_init() {}

FileResult
file_create(Allocator* alloc, String path, FileMode mode, FileAccessFlags access, File** file) {
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
      .alloc  = alloc,
  };
  return FileResult_Success;
}

FileResult file_temp(Allocator* alloc, File** file) {
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
      .alloc  = alloc,
  };
  return FileResult_Success;
}

void file_destroy(File* file) {
  diag_assert_msg(file->alloc, "Invalid file");
  close(file->handle);
  alloc_free_t(file->alloc, file);
}

FileResult file_write_sync(File* file, const String data) {
  diag_assert(file);

  for (u8* itr = mem_begin(data); itr != mem_end(data);) {
    const int res = write(file->handle, itr, mem_end(data) - itr);
    if (res > 0) {
      itr += res;
      continue;
    }
    switch (errno) {
    case EAGAIN:
    case EINTR:
      continue; // Retry on interupt.
    }
    return fileresult_from_errno();
  }
  return FileResult_Success;
}

FileResult file_read_sync(File* file, DynString* dynstr) {
  diag_assert(file);

  /**
   * TODO: Consider reserving space in the output DynString and directly reading into that to avoid
   * the copy. Downside is for small reads we would grow the DynString unnecessarily.
   */

  Mem readBuffer = mem_stack(usize_kibibyte);
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
      continue; // Retry on interupt.
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
