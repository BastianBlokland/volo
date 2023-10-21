#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_path.h"
#include "core_process.h"

spec(process) {
  String helperPath;

  setup() {
    const String parentPath = path_parent(g_path_executable);
    const String helperName = string_lit("test_lib_core_helper");
    helperPath              = string_dup(g_alloc_heap, path_build_scratch(parentPath, helperName));
  }

  it("can wait until execution is finished") {
    const String       args[0];
    const u32          argCount = array_elems(args);
    const ProcessFlags flags    = ProcessFlags_None;
    Process*           child    = process_create(g_alloc_heap, helperPath, args, argCount, flags);

    check_eq_int(process_block(child), 0);

    process_destroy(child);
  }

  it("can pass arguments") {
    const String       args[]   = {string_lit("--exitcode"), string_lit("42")};
    const u32          argCount = array_elems(args);
    const ProcessFlags flags    = ProcessFlags_None;
    Process*           child    = process_create(g_alloc_heap, helperPath, args, argCount, flags);

    check_eq_int(process_block(child), 42);

    process_destroy(child);
  }

  teardown() { string_free(g_alloc_heap, helperPath); }
}
