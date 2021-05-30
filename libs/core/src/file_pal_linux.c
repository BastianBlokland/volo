#include "core_diag.h"
#include "core_file.h"
#include "file_internal.h"
#include <errno.h>
#include <unistd.h>

File* g_file_stdin  = &(File){.handle = 0};
File* g_file_stdout = &(File){.handle = 1};
File* g_file_stderr = &(File){.handle = 2};

static FileResult file_result_from_errno() {
  switch (errno) {
  case EACCES:
  case EPERM:
  case EROFS:
    return File_NoAccess;
  case ETXTBSY:
    return File_Locked;
  case EDQUOT:
  case ENOSPC:
    return File_DiskFull;
  case ENOENT:
    return File_NotFound;
  case EMFILE:
  case ENFILE:
    return File_TooManyOpenFiles;
  }
  return File_UnknownError;
}

void file_pal_init() {}

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
    return file_result_from_errno();
  }
  return File_Success;
}

FileResult file_read_sync(File* file, DynString* dynstr) {
  diag_assert(file);

  Mem readBuffer = mem_stack(1024);
  while (true) {
    const ssize_t res = read(file->handle, readBuffer.ptr, readBuffer.size);
    if (res > 0) {
      dynstring_append(dynstr, mem_slice(readBuffer, 0, res));
      return File_Success;
    }
    if (res == 0) {
      return File_NoDataAvailable;
    }
    switch (errno) {
    case EINTR:
      continue; // Retry on interupt.
    }
    return file_result_from_errno();
  }
}
