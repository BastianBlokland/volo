#pragma once
#include "core_file.h"

#ifdef VOLO_LINUX
typedef int FileHandle;
#elif defined(VOLO_WIN32)
typedef void* FileHandle;
#else
_Static_assert(false, "Unsupported platform");
#endif

struct sFile {
  FileHandle      handle;
  FileAccessFlags access;
  void*           mapping;
  Allocator*      alloc;
};

void file_pal_init();
