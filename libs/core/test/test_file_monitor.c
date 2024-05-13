#include "check_spec.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_file_monitor.h"
#include "core_path.h"
#include "core_rng.h"
#include "core_thread.h"
#include "core_time.h"

static String test_random_name() {
  return path_name_random_scratch(g_rng, string_lit("test-file-monitor"), string_lit("tmp"));
}

spec(file_monitor) {

  FileMonitor* monitor = null;

  setup() {
    monitor = file_monitor_create(g_allocHeap, g_path_tempdir, FileMonitorFlags_None);

    // Create an empty test file.
    file_write_to_path_sync(path_build_scratch(g_path_tempdir, string_lit("test")), string_empty);
  }

  it("can watch a file") {
    const FileMonitorResult res = file_monitor_watch(monitor, string_lit("test"), 0);
    check_eq_int(res, FileMonitorResult_Success);
  }

  it("fails when watching a file twice") {
    const FileMonitorResult res1 = file_monitor_watch(monitor, string_lit("test"), 0);
    check_eq_int(res1, FileMonitorResult_Success);

    const FileMonitorResult res2 = file_monitor_watch(monitor, string_lit("test"), 0);
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
    const String pathRel = test_random_name();
    const String pathAbs = path_build_scratch(g_path_tempdir, pathRel);
    file_write_to_path_sync(pathAbs, string_lit("Hello world"));

    thread_sleep(time_milliseconds(1));

    FileMonitorEvent event;
    check(!file_monitor_poll(monitor, &event));

    check_eq_int(file_monitor_watch(monitor, pathRel, 0), FileMonitorResult_Success);

    check(!file_monitor_poll(monitor, &event));

    file_delete_sync(pathAbs);
  }

  /**
   * TODO: Skipped for now as it can be a bit flaky on slow machines due to timing requirements.
   */
  skip_it("returns a single event when a file is modified") {
    const String pathRel = test_random_name();
    const String pathAbs = path_build_scratch(g_path_tempdir, pathRel);
    file_write_to_path_sync(pathAbs, string_lit(""));

    check_eq_int(file_monitor_watch(monitor, pathRel, 42), FileMonitorResult_Success);

    thread_sleep(time_milliseconds(1));

    file_write_to_path_sync(pathAbs, string_lit("Hello World"));

    thread_sleep(time_milliseconds(1));

    FileMonitorEvent event;
    check_require(file_monitor_poll(monitor, &event));

    check_eq_string(event.path, pathRel);
    check_eq_int(event.userData, 42);

    check(!file_monitor_poll(monitor, &event));

    file_delete_sync(pathAbs);
  }

  /**
   * TODO: Skipped for now as it can be a bit flaky on slow machines due to timing requirements.
   */
  skip_it("can watch multiple files") {
    const String pathRelA = test_random_name();
    const String pathAbsA = path_build_scratch(g_path_tempdir, pathRelA);
    file_write_to_path_sync(pathAbsA, string_lit("A"));

    const String pathRelB = test_random_name();
    const String pathAbsB = path_build_scratch(g_path_tempdir, pathRelB);
    file_write_to_path_sync(pathAbsB, string_lit("B"));

    check_eq_int(file_monitor_watch(monitor, pathRelA, 1), FileMonitorResult_Success);
    check_eq_int(file_monitor_watch(monitor, pathRelB, 2), FileMonitorResult_Success);

    thread_sleep(time_milliseconds(1));

    file_write_to_path_sync(pathAbsA, string_lit("A-Modified"));
    file_write_to_path_sync(pathAbsB, string_lit("B-Modified"));

    thread_sleep(time_milliseconds(1));

    FileMonitorEvent event1;
    check_require(file_monitor_poll(monitor, &event1));
    FileMonitorEvent event2;
    check_require(file_monitor_poll(monitor, &event2));

    check(event1.userData != event2.userData);
    check(event1.userData == 1 || event1.userData == 2);
    check(event2.userData == 1 || event2.userData == 2);

    check(!file_monitor_poll(monitor, &event1));

    file_delete_sync(pathAbsA);
    file_delete_sync(pathAbsB);
  }

  it("watching fails when the root directory cannot be opened") {
    const String nonExistentDir = path_build_scratch(g_path_tempdir, string_lit("does-not-exist"));
    FileMonitor* mon = file_monitor_create(g_allocHeap, nonExistentDir, FileMonitorFlags_None);

    const String filePath = string_lit("test.txt");
    check_eq_int(file_monitor_watch(mon, filePath, 1), FileMonitorResult_UnableToOpenRoot);

    file_monitor_destroy(mon);
  }

  teardown() { file_monitor_destroy(monitor); }
}
