#include "check_spec.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_file_monitor.h"
#include "core_path.h"
#include "core_rng.h"
#include "core_thread.h"
#include "core_time.h"

static String test_random_path() {
  return path_build_scratch(
      g_path_tempdir,
      path_name_random_scratch(g_rng, string_lit("test-file-monitor"), string_lit("tmp")));
}

spec(file_monitor) {

  FileMonitor* monitor = null;

  setup() { monitor = file_monitor_create(g_alloc_heap); }

  it("can watch a file") {
    const FileMonitorResult res = file_monitor_watch(monitor, g_path_executable, 0);
    check_eq_int(res, FileMonitorResult_Success);
  }

  it("fails when watching a file twice") {
    const FileMonitorResult res1 = file_monitor_watch(monitor, g_path_executable, 0);
    check_eq_int(res1, FileMonitorResult_Success);

    const FileMonitorResult res2 = file_monitor_watch(monitor, g_path_executable, 0);
    check_eq_int(res2, FileMonitorResult_AlreadyWatching);
  }

  it("fails when watching a file that does not exist") {
    const FileMonitorResult res = file_monitor_watch(monitor, string_lit("does-not-exist-42"), 0);
    check_eq_int(res, FileMonitorResult_FileDoesNotExist);
  }

  it("returns false when polling without watching a file") {
    FileMonitorEvent event;
    check(!file_monitor_poll(monitor, &event));
  }

  it("returns false when polling when no modifications have happened") {
    const String path = test_random_path();
    file_write_to_path_sync(path, string_lit("Hello world"));

    check_eq_int(file_monitor_watch(monitor, path, 0), FileMonitorResult_Success);

    FileMonitorEvent event;
    check(!file_monitor_poll(monitor, &event));

    file_delete_sync(path);
  }

  it("returns a single event when a file is modified") {
    const String path = test_random_path();
    file_write_to_path_sync(path, string_lit(""));

    check_eq_int(file_monitor_watch(monitor, path, 42), FileMonitorResult_Success);

    thread_sleep(time_millisecond);

    file_write_to_path_sync(path, string_lit("Hello World"));

    FileMonitorEvent event;
    check_require(file_monitor_poll(monitor, &event));

    check_eq_string(event.path, path);
    check_eq_int(event.userData, 42);

    check(!file_monitor_poll(monitor, &event));

    file_delete_sync(path);
  }

  it("can watch multiple files") {
    const String pathA = test_random_path();
    file_write_to_path_sync(pathA, string_lit("A"));

    const String pathB = test_random_path();
    file_write_to_path_sync(pathB, string_lit("B"));

    check_eq_int(file_monitor_watch(monitor, pathA, 1), FileMonitorResult_Success);
    check_eq_int(file_monitor_watch(monitor, pathB, 2), FileMonitorResult_Success);

    thread_sleep(time_millisecond);

    file_write_to_path_sync(pathA, string_lit("A-Modified"));
    file_write_to_path_sync(pathB, string_lit("B-Modified"));

    FileMonitorEvent event1;
    check_require(file_monitor_poll(monitor, &event1));
    FileMonitorEvent event2;
    check_require(file_monitor_poll(monitor, &event2));

    check(event1.userData != event2.userData);
    check(event1.userData == 1 || event1.userData == 2);
    check(event2.userData == 1 || event2.userData == 2);

    check(!file_monitor_poll(monitor, &event1));

    file_delete_sync(pathA);
    file_delete_sync(pathB);
  }

  teardown() { file_monitor_destroy(monitor); }
}
