#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_file_monitor.h"
#include "core_path.h"
#include "core_thread.h"

#include <errno.h>
#include <limits.h>
#include <sys/inotify.h>
#include <unistd.h>

#define monitor_inotifyMask IN_CLOSE_WRITE
#define monitor_event_size (sizeof(struct inotify_event) + NAME_MAX + 1)
#define monitor_path_chunk_size (16 * usize_kibibyte)

// Internal flags.
enum {
  FileMonitorFlags_RootDirectoryInaccessible = 1 << (FileMonitorFlags_Count + 0),
};

typedef struct {
  int    wd;
  String path;
  u64    userData;
} FileWatch;

struct sFileMonitor {
  Allocator*       alloc;
  Allocator*       allocPath; // (chunked) bump allocator for paths.
  ThreadMutex      mutex;
  FileMonitorFlags flags;
  int              fd;
  String           rootPath;
  DynArray         watches; // FileWatch[], kept sorted on the wd.

  Mem bufferRemaining;

  ALIGNAS(alignof(struct inotify_event))
  u8 buffer[monitor_event_size * 10]; // Big enough for atleast 10 events.
};

static const char* to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_alloc_scratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

static i8 watch_compare_wd(const void* a, const void* b) {
  return compare_u32(field_ptr(a, FileWatch, wd), field_ptr(b, FileWatch, wd));
}

static FileMonitorResult result_from_errno(void) {
  switch (errno) {
  case EACCES:
    return FileMonitorResult_NoAccess;
  case EEXIST:
    return FileMonitorResult_AlreadyWatching;
  case ENAMETOOLONG:
    return FileMonitorResult_PathTooLong;
  case ENOENT:
    return FileMonitorResult_FileDoesNotExist;
  case ENOSPC:
    return FileMonitorResult_WatchesLimitReached;
  }
  return FileMonitorResult_UnknownError;
}

static FileWatch* file_watch_by_wd(FileMonitor* monitor, const int wd) {
  return dynarray_search_binary(
      &monitor->watches, watch_compare_wd, mem_struct(FileWatch, .wd = wd).ptr);
}

/**
 * Register a new watch-descriptor.
 * NOTE: Should only be called while having monitor->mutex locked.
 */
static FileMonitorResult file_watch_register_locked(
    FileMonitor* monitor, const int wd, const String path, const u64 userData) {
#if !defined(IN_MASK_CREATE)
  /**
   * Without 'IN_MASK_CREATE' we need to manually check if we already had a watch for this path.
   */
  if (file_watch_by_wd(monitor, wd)) {
    return FileMonitorResult_AlreadyWatching;
  }
#endif
  const FileWatch watch = {
      .wd       = wd,
      .path     = string_dup(monitor->allocPath, path),
      .userData = userData,
  };
  *dynarray_insert_sorted_t(&monitor->watches, FileWatch, watch_compare_wd, &watch) = watch;
  return FileMonitorResult_Success;
}

/**
 * NOTE: Should only be called while having monitor->mutex locked.
 */
static bool file_monitor_poll_locked(FileMonitor* monitor, FileMonitorEvent* out) {
  /**
   * If our buffer is empty then read new events from the kernel.
   */
  if (!monitor->bufferRemaining.size) {
    const ssize_t len = read(monitor->fd, monitor->buffer, sizeof(monitor->buffer));
    if (len < 0) {
      return false; // No events available.
    }
    monitor->bufferRemaining = mem_create(monitor->buffer, (usize)len);
  }

  /**
   * Return the first valid event from the buffer.
   */
  while (monitor->bufferRemaining.size) {
    const struct inotify_event* event     = monitor->bufferRemaining.ptr;
    const usize                 eventSize = sizeof(struct inotify_event) + event->len;
    monitor->bufferRemaining              = mem_consume(monitor->bufferRemaining, eventSize);

    const FileWatch* watch = file_watch_by_wd(monitor, event->wd);
    if (UNLIKELY(!watch)) {
      continue;
    }
    *out = (FileMonitorEvent){
        .path     = watch->path,
        .userData = watch->userData,
    };
    return true;
  }

  return false; // No event was valid.
}

FileMonitor* file_monitor_create(Allocator* alloc, const String rootPath, FileMonitorFlags flags) {
  const String rootPathAbs = path_build_scratch(rootPath);

  int inotifyFlags = 0;
  if (!(flags & FileMonitorFlags_Blocking)) {
    inotifyFlags |= IN_NONBLOCK;
  }
  const int fd = inotify_init1(inotifyFlags);
  if (fd == -1) {
    diag_crash_msg("inotify_init() failed: {}", fmt_int(errno));
  }

  /**
   * Stat the root-path for more consistent error messages across platforms.
   */
  if (file_stat_path_sync(rootPathAbs).type != FileType_Directory) {
    flags |= FileMonitorFlags_RootDirectoryInaccessible;
  }

  FileMonitor* monitor = alloc_alloc_t(alloc, FileMonitor);

  *monitor = (FileMonitor){
      .alloc     = alloc,
      .allocPath = alloc_chunked_create(g_alloc_page, alloc_bump_create, monitor_path_chunk_size),
      .mutex     = thread_mutex_create(alloc),
      .flags     = flags,
      .fd        = fd,
      .watches   = dynarray_create_t(alloc, FileWatch, 64),
  };
  monitor->rootPath = string_dup(monitor->allocPath, rootPathAbs);

  return monitor;
}

void file_monitor_destroy(FileMonitor* monitor) {
  close(monitor->fd);

  alloc_chunked_destroy(monitor->allocPath);
  dynarray_destroy(&monitor->watches);
  thread_mutex_destroy(monitor->mutex);
  alloc_free_t(monitor->alloc, monitor);
}

FileMonitorResult file_monitor_watch(FileMonitor* monitor, const String path, const u64 userData) {
  diag_assert(!path_is_absolute(path));

  if (UNLIKELY(monitor->flags & FileMonitorFlags_RootDirectoryInaccessible)) {
    return FileMonitorResult_UnableToOpenRoot;
  }

  // TODO: We can avoid one copy by combining the absolute path building and the null terminating.
  const String pathAbs         = path_build_scratch(monitor->rootPath, path);
  const char*  pathAbsNullTerm = to_null_term_scratch(pathAbs);
  u32          mask            = monitor_inotifyMask;
#if defined(IN_MASK_CREATE)
  mask |= IN_MASK_CREATE;
#endif
  const int wd = inotify_add_watch(monitor->fd, pathAbsNullTerm, mask);
  if (wd < 0) {
    return result_from_errno();
  }
  thread_mutex_lock(monitor->mutex);
  const FileMonitorResult res = file_watch_register_locked(monitor, wd, path, userData);
  thread_mutex_unlock(monitor->mutex);
  return res;
}

bool file_monitor_poll(FileMonitor* monitor, FileMonitorEvent* out) {
  thread_mutex_lock(monitor->mutex);
  const bool res = file_monitor_poll_locked(monitor, out);
  thread_mutex_unlock(monitor->mutex);
  return res;
}
