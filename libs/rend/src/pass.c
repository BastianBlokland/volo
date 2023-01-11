#include "core_annotation.h"
#include "core_array.h"
#include "rend_pass.h"

String rend_pass_name(const RendPass pass) {
  static const String g_names[] = {
      string_static("geometry"),
      string_static("forward"),
      string_static("post"),
      string_static("shadow"),
      string_static("ambient-occlusion"),
  };
  ASSERT(array_elems(g_names) == RendPass_Count, "Incorrect number of names");
  return g_names[pass];
}
