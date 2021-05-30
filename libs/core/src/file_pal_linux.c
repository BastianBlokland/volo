#include "core_diag.h"
#include "core_file.h"
#include "file_internal.h"
#include "linux/limits.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

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
    diag_assert_msg(false, "Invalid FileMode");
  }

  if (access & FileAccess_Read) {
    flags |= O_RDONLY;
  }
  if (access & FileAccess_Write) {
    flags |= O_WRONLY;
  }

  const int newFilePerms = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // RW for owner, and R for others.
  const int fd           = open(pathBuffer.ptr, flags, newFilePerms);
  if (fd < 0) {
    return fileresult_from_errno();
  }

  Mem allocation = alloc_alloc(alloc, sizeof(File));
  *file          = mem_as_t(allocation, File);
  **file         = (File){
      .handle     = fd,
      .alloc      = alloc,
      .allocation = allocation,
  };
  return FileResult_Success;
}

void file_destroy(File* file) {
  diag_assert_msg(file->alloc, "Invalid file");
  close(file->handle);
  alloc_free(file->alloc, file->allocation);
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

  Mem readBuffer = mem_stack(1024);
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
