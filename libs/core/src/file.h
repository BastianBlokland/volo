#pragma once
#include "core/dynarray.h"
#include "core/file.h"

typedef struct {
#if defined(VOLO_WIN32)
  uptr handle;
#endif
  usize offset; // Offset into the file.
  void* ptr;
  usize size;
} FileMapping;

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
  Allocator*      alloc;
  DynArray        mappings; // FileMapping[], sorted on ptr.
};

void       file_pal_init(void);
FileResult file_pal_create(Allocator*, String path, FileMode, FileAccessFlags, File** file);
FileResult file_pal_temp(Allocator*, File** file);
void       file_pal_destroy(File*);

FileResult file_pal_map(File*, usize offset, usize size, FileHints, FileMapping* out);
FileResult file_pal_unmap(File*, FileMapping* mapping);

/**
 * Synchonously create a single directory.
 * Pre-condition: Parent directory must exist.
 */
FileResult file_pal_create_dir_single_sync(String path);
