#pragma once
#include "core_file.h"

#if defined(VOLO_LINUX)
typedef int FileHandle;
#elif defined(VOLO_WIN32)
typedef void* FileHandle;
#else
#error Unsupported platform
#endif

struct sFile {
  FileHandle      handle;
  FileAccessFlags access;
  void*           mapping;
  Allocator*      alloc;
};

void       file_pal_init(void);
FileResult file_pal_create(Allocator*, String path, FileMode, FileAccessFlags, File** file);
FileResult file_pal_temp(Allocator*, File** file);
void       file_pal_destroy(File*);

/**
 * Synchonously create a single directory.
 * Pre-condition: Parent directory must exist.
 */
FileResult file_pal_create_dir_single_sync(String path);
