#pragma once
#include "core_file.h"

#if defined(VOLO_LINUX)
typedef int FileHandle;
#elif defined(VOLO_WIN32)
typedef void* FileHandle;
#else
ASSERT(false, "Unsupported platform");
#endif

struct sFile {
  FileHandle      handle;
  FileAccessFlags access;
  void*           mapping;
  Allocator*      alloc;
};

void file_pal_init();

/**
 * Synchonously create a single directory.
 * Pre-condition: Parent directory must exist.
 */
FileResult file_pal_create_dir_single_sync(String path);
