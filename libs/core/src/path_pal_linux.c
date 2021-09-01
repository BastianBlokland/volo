#include "core_diag.h"
#include "core_env.h"

#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include "path_internal.h"

static String path_canonize_to_output_buffer(Mem outputBuffer, String path) {
  DynString writer = dynstring_create_over(outputBuffer);
  path_canonize(&writer, path);

  String result = dynstring_view(&writer);
  dynstring_destroy(&writer);
  return result;
}

String path_pal_workingdir(Mem outputBuffer) {
  Mem tmp = mem_stack(path_pal_max_size);
  if (!getcwd(tmp.ptr, tmp.size)) {
    diag_crash_msg("getcwd() failed");
  }
  return path_canonize_to_output_buffer(outputBuffer, string_from_null_term(tmp.ptr));
}

String path_pal_executable(Mem outputBuffer) {
  Mem tmp = mem_stack(PATH_MAX);
  if (!realpath("/proc/self/exe", tmp.ptr)) {
    diag_crash_msg("failed to resolve '/proc/self/exe'");
  }
  return path_canonize_to_output_buffer(outputBuffer, string_from_null_term(tmp.ptr));
}

String path_pal_tempdir(Mem outputBuffer) {
  DynString tmpWriter = dynstring_create_over(mem_stack(PATH_MAX));
  String    result;

  if (env_var(string_lit("TMPDIR"), &tmpWriter)) {
    result = path_canonize_to_output_buffer(outputBuffer, dynstring_view(&tmpWriter));
    goto Ret;
  }

  result = path_canonize_to_output_buffer(outputBuffer, string_lit("/tmp"));

Ret:
  dynstring_destroy(&tmpWriter);
  return result;
}
