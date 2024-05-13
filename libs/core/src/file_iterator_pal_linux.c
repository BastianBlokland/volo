#include "core_alloc.h"
#include "core_diag.h"
#include "core_file_iterator.h"
#include "core_path.h"

#include <dirent.h>
#include <errno.h>

struct sFileIterator {
  Allocator* alloc;
  DIR*       dirStream;
  int        dirStreamErr;
};

static const char* to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

static FileIteratorResult result_from_errno(const int err) {
  switch (err) {
  case EACCES:
    return FileIteratorResult_NoAccess;
  case ENOENT:
    return FileIteratorResult_DirectoryDoesNotExist;
  case EMFILE:
  case ENFILE:
    return FileIteratorResult_TooManyOpenFiles;
  case ENOTDIR:
    return FileIteratorResult_PathIsNotADirectory;
  }
  return FileIteratorResult_UnknownError;
}

static FileType file_type_from_dtype(const u8 dtype) {
  switch (dtype) {
  case DT_REG:
    return FileType_Regular;
  case DT_DIR:
    return FileType_Directory;
  default:
    return FileType_Unknown;
  }
}

FileIterator* file_iterator_create(Allocator* alloc, const String path) {
  // TODO: We can avoid one copy by combining the absolute path building and the null terminating.
  const String pathAbs         = path_build_scratch(path);
  const char*  pathAbsNullTerm = to_null_term_scratch(pathAbs);

  DIR*      dirStream    = opendir(pathAbsNullTerm);
  const int dirStreamErr = errno;

  FileIterator* itr = alloc_alloc_t(alloc, FileIterator);

  *itr = (FileIterator){
      .alloc        = alloc,
      .dirStream    = dirStream,
      .dirStreamErr = dirStreamErr,
  };

  return itr;
}

void file_iterator_destroy(FileIterator* itr) {
  if (itr->dirStream) {
    const int closeRes = closedir(itr->dirStream);
    if (UNLIKELY(closeRes == -1)) {
      diag_crash_msg("closedir() failed: {}", fmt_int(errno));
    }
  }
  alloc_free_t(itr->alloc, itr);
}

FileIteratorResult file_iterator_next(FileIterator* itr, FileIteratorEntry* out) {
  if (UNLIKELY(!itr->dirStream)) {
    return result_from_errno(itr->dirStreamErr);
  }
  for (;;) {
    errno                 = 0;
    struct dirent* dirEnt = readdir(itr->dirStream);
    if (!dirEnt) {
      return errno ? result_from_errno(errno) : FileIteratorResult_End;
    }
    const String name = string_from_null_term(dirEnt->d_name);
    if (string_eq(name, string_lit(".")) || string_eq(name, string_lit(".."))) {
      continue; // Skip '.' and '..' entries.
    }
    *out = (FileIteratorEntry){
        .type = file_type_from_dtype(dirEnt->d_type),
        .name = name,
    };
    return FileIteratorResult_Found;
  }
}
