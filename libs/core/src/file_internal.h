#pragma once

#ifdef VOLO_LINUX
typedef int FileHandle;
#elif defined(VOLO_WIN32)
typedef void* FileHandle;
#else
diag_static_assert(false, "Unsupported platform");
#endif

struct sFile {
  FileHandle handle;
};
