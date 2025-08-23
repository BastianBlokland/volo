#include "core/alloc.h"
#include "core/diag.h"
#include "core/file_iterator.h"
#include "core/path.h"
#include "core/winutils.h"

#include <Windows.h>

struct sFileIterator {
  Allocator* alloc;
  String     path;
  HANDLE     findHandle;
};

static HANDLE file_find_first(const String path, WIN32_FIND_DATA* out) {
  // TODO: We can avoid some string copies by combining these steps.
  const String pathAbs            = path_build_scratch(path);
  const String searchQuery        = fmt_write_scratch("{}/*", fmt_text(pathAbs));
  const Mem    searchQueryWideStr = winutils_to_widestr_scratch(searchQuery);

  return FindFirstFileEx(
      (const wchar_t*)searchQueryWideStr.ptr, FindExInfoBasic, out, FindExSearchNameMatch, null, 0);
}

static bool file_find_next(HANDLE findHandle, WIN32_FIND_DATA* out) {
  return FindNextFile(findHandle, out);
}

static void file_find_close(HANDLE findHandle) {
  if (!FindClose(findHandle)) {
    const DWORD err = GetLastError();
    diag_crash_msg(
        "FindClose() failed: {}, {}", fmt_int((u64)err), fmt_text(winutils_error_msg_scratch(err)));
  }
}

static FileIteratorResult file_iterator_result_from_error(const DWORD err) {
  switch (err) {
  case ERROR_NO_MORE_FILES:
    return FileIteratorResult_End;
  case ERROR_ACCESS_DENIED:
    return FileIteratorResult_NoAccess;
  case ERROR_PATH_NOT_FOUND:
  case ERROR_FILE_NOT_FOUND:
    return FileIteratorResult_DirectoryDoesNotExist;
  case ERROR_TOO_MANY_OPEN_FILES:
    return FileIteratorResult_TooManyOpenFiles;
  case ERROR_DIRECTORY:
    return FileIteratorResult_PathIsNotADirectory;
  }
  return FileIteratorResult_UnknownError;
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

FileIterator* file_iterator_create(Allocator* alloc, const String path) {
  diag_assert(!string_is_empty(path));

  FileIterator* itr = alloc_alloc_t(alloc, FileIterator);

  *itr = (FileIterator){
      .alloc      = alloc,
      .path       = string_dup(alloc, path),
      .findHandle = INVALID_HANDLE_VALUE,
  };

  return itr;
}

void file_iterator_destroy(FileIterator* itr) {
  if (itr->findHandle != INVALID_HANDLE_VALUE) {
    file_find_close(itr->findHandle);
  }
  string_free(itr->alloc, itr->path);
  alloc_free_t(itr->alloc, itr);
}

FileIteratorResult file_iterator_next(FileIterator* itr, FileIteratorEntry* out) {
  WIN32_FIND_DATA findData;
  for (;;) {
    if (itr->findHandle == INVALID_HANDLE_VALUE) {
      itr->findHandle = file_find_first(itr->path, &findData);
      if (itr->findHandle == INVALID_HANDLE_VALUE) {
        return file_iterator_result_from_error(GetLastError());
      }
    } else {
      if (!file_find_next(itr->findHandle, &findData)) {
        return file_iterator_result_from_error(GetLastError());
      }
    }
    const size_t nameWideCharCount = wcslen(findData.cFileName);
    const String name = winutils_from_widestr_scratch(findData.cFileName, nameWideCharCount);

    if (string_eq(name, string_lit(".")) || string_eq(name, string_lit(".."))) {
      continue; // Skip '.' and '..' entries.
    }

    *out = (FileIteratorEntry){
        .type = file_type_from_attributes(findData.dwFileAttributes),
        .name = name,
    };
    return FileIteratorResult_Found;
  }
}
