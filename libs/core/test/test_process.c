#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_dynstring.h"
#include "core_file.h"
#include "core_path.h"
#include "core_process.h"
#include "core_signal.h"
#include "core_thread.h"
#include "core_time.h"

spec(process) {
  String    helperPath;
  DynString buffer;

  setup() {
    const String parentPath = path_parent(g_pathExecutable);
    const String helperName = string_lit("test_lib_core_helper");
    helperPath              = string_dup(g_allocHeap, path_build_scratch(parentPath, helperName));

    buffer = dynstring_create(g_allocHeap, 1 * usize_kibibyte);
  }

  it("fails when file does not exist") {
    const String file  = string_lit("executable_that_doest_not_exist_42");
    Process*     child = process_create(g_allocHeap, file, null, 0, ProcessFlags_None);

    check_eq_int(process_block(child), ProcessExitCode_ExecutableNotFound);

    process_destroy(child);
  }

  it("can wait until execution is finished") {
    Process* child = process_create(g_allocHeap, helperPath, null, 0, ProcessFlags_None);

    check_require(process_start_result(child) == ProcessResult_Success);
    check_eq_int(process_block(child), 0);

    process_destroy(child);
  }

  it("can pass arguments") {
    const String       args[]   = {string_lit("--exitcode"), string_lit("42")};
    const u32          argCount = array_elems(args);
    const ProcessFlags flags    = ProcessFlags_None;
    Process*           child    = process_create(g_allocHeap, helperPath, args, argCount, flags);

    check_require(process_start_result(child) == ProcessResult_Success);

    check_eq_int(process_block(child), 42);
    check(!process_poll(child));

    process_destroy(child);
  }

  it("can send a kill signal") {
    const String       args[]   = {string_lit("--block")};
    const u32          argCount = array_elems(args);
    const ProcessFlags flags    = ProcessFlags_None;
    Process*           child    = process_create(g_allocHeap, helperPath, args, argCount, flags);

    check_require(process_start_result(child) == ProcessResult_Success);
    check_require(process_poll(child));

    check_eq_int(process_signal(child, Signal_Kill), ProcessResult_Success);

    check_eq_int(process_block(child), ProcessExitCode_TerminatedBySignal);
    check(!process_poll(child));

    process_destroy(child);
  }

  /**
   * TODO: Skipped for now as it can be a bit flaky on slow machines due to timing requirements.
   */
  skip_it("can send an interrupt signal") {
    const String       args[] = {string_lit("--wait"), string_lit("--exitcode"), string_lit("42")};
    const u32          argCount = array_elems(args);
    const ProcessFlags flags    = ProcessFlags_NewGroup;
    Process*           child    = process_create(g_allocHeap, helperPath, args, argCount, flags);

    check_require(process_start_result(child) == ProcessResult_Success);
    check_require(process_poll(child));

    thread_sleep(time_milliseconds(50)); // Wait for child to setup interrupt handler.
    check_eq_int(process_signal(child, Signal_Interrupt), ProcessResult_Success);

    check_eq_int(process_block(child), 42);
    check(!process_poll(child));

    process_destroy(child);
  }

  it("can read std-out") {
    const String       args[]   = {string_lit("--greet")};
    const u32          argCount = array_elems(args);
    const ProcessFlags flags    = ProcessFlags_PipeStdOut;
    Process*           child    = process_create(g_allocHeap, helperPath, args, argCount, flags);

    check_require(process_start_result(child) == ProcessResult_Success);
    check_require(process_poll(child));

    check_eq_int(file_read_to_end_sync(process_pipe_out(child), &buffer), FileResult_Success);
    check_eq_string(dynstring_view(&buffer), string_lit("Hello Out\n"));

    check_eq_int(process_block(child), 0);
    check(!process_poll(child));

    process_destroy(child);
  }

  it("can read std-err") {
    const String       args[]   = {string_lit("--greetErr")};
    const u32          argCount = array_elems(args);
    const ProcessFlags flags    = ProcessFlags_PipeStdErr;
    Process*           child    = process_create(g_allocHeap, helperPath, args, argCount, flags);

    check_require(process_start_result(child) == ProcessResult_Success);
    check_require(process_poll(child));

    check_eq_int(file_read_to_end_sync(process_pipe_err(child), &buffer), FileResult_Success);
    check_eq_string(dynstring_view(&buffer), string_lit("Hello Err\n"));

    check_eq_int(process_block(child), 0);

    process_destroy(child);
  }

  it("can write to std-in") {
    const String       args[]   = {string_lit("--countInChars")};
    const u32          argCount = array_elems(args);
    const ProcessFlags flags    = ProcessFlags_PipeStdIn;
    Process*           child    = process_create(g_allocHeap, helperPath, args, argCount, flags);

    check_require(process_start_result(child) == ProcessResult_Success);
    check_require(process_poll(child));

    const String str = string_lit("Hello World!");
    check_eq_int(file_write_sync(process_pipe_in(child), str), FileResult_Success);
    process_pipe_close_in(child);

    check_eq_int(process_block(child), str.size);

    process_destroy(child);
  }

  teardown() {
    string_free(g_allocHeap, helperPath);
    dynstring_destroy(&buffer);
  }
}
