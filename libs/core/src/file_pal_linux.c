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

FileResult file_write_sync(File* file, String data) {
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
      continue; // Retry.
    }
    return file_result_from_errno();
  }
  return File_Success;
}
