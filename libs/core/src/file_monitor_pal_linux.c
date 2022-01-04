#include "core_alloc.h"
#include "core_diag.h"
#include "core_file_monitor.h"

#include <errno.h>
#include <limits.h>
#include <sys/inotify.h>
#include <unistd.h>

#define monitor_inotifyMask IN_CLOSE_WRITE
#define monitor_event_size (sizeof(struct inotify_event) + NAME_MAX + 1)

typedef struct {
  int    wd;
  String path;
  u64    userData;
} FileWatch;

struct sFileMonitor {
  Allocator* alloc;
  int        fd;
  DynArray   watches; // FileWatch[], kept sorted on the wd.

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

static FileMonitorResult result_from_errno() {
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

FileMonitor* file_monitor_create(Allocator* alloc) {
  const int fd = inotify_init1(IN_NONBLOCK);
  if (fd == -1) {
    diag_crash_msg("inotify_init() failed: {}", fmt_int(errno));
  }

  FileMonitor* monitor = alloc_alloc_t(alloc, FileMonitor);
  *monitor             = (FileMonitor){
      .alloc   = alloc,
      .fd      = fd,
      .watches = dynarray_create_t(alloc, FileWatch, 64),
  };
  return monitor;
}

void file_monitor_destroy(FileMonitor* monitor) {
  close(monitor->fd);

  dynarray_for_t(&monitor->watches, FileWatch, watch) { string_free(monitor->alloc, watch->path); }
  dynarray_destroy(&monitor->watches);

  alloc_free_t(monitor->alloc, monitor);
}

FileMonitorResult file_monitor_watch(FileMonitor* monitor, const String path, const u64 userData) {
  const char* pathNullTerm = to_null_term_scratch(path);
  u32         mask         = monitor_inotifyMask;
#ifdef IN_MASK_CREATE
  mask |= IN_MASK_CREATE;
#endif
  const int wd = inotify_add_watch(monitor->fd, pathNullTerm, mask);
  if (wd < 0) {
    return result_from_errno();
  }
#ifndef IN_MASK_CREATE
  /**
   * Without 'IN_MASK_CREATE' we need to manually check if we already had a watch for this path.
   */
  if (file_watch_by_wd(monitor, wd)) {
    return FileMonitorResult_AlreadyWatching;
  }
#endif
  const FileWatch watch = {
      .wd       = wd,
      .path     = string_dup(monitor->alloc, path),
      .userData = userData,
  };
  *dynarray_insert_sorted_t(&monitor->watches, FileWatch, watch_compare_wd, &watch) = watch;
  return FileMonitorResult_Success;
}

bool file_monitor_poll(FileMonitor* monitor, FileMonitorEvent* out) {
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
