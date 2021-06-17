#include "core_diag.h"
#include "path_internal.h"
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

String path_pal_workingdir(Mem outputBuffer) {
  Mem tmp = mem_stack(path_pal_max_size);
  if (!getcwd(tmp.ptr, tmp.size)) {
    diag_assert_fail("getcwd() failed");
  }

  DynString writer = dynstring_create_over(outputBuffer);
  path_canonize(&writer, string_from_null_term(tmp.ptr));

  String result = dynstring_view(&writer);
  dynstring_destroy(&writer);
  return result;
}

String path_pal_executable(Mem outputBuffer) {
  Mem tmp = mem_stack(PATH_MAX);
  if (!realpath("/proc/self/exe", tmp.ptr)) {
    diag_assert_fail("failed to resolve '/proc/self/exe'");
  }

  DynString writer = dynstring_create_over(outputBuffer);
  path_canonize(&writer, string_from_null_term(tmp.ptr));

  String result = dynstring_view(&writer);
  dynstring_destroy(&writer);
  return result;
}
