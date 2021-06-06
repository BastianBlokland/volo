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
  FileHandle handle;
  Allocator* alloc;
  Mem        allocation;
};

void file_pal_init();
