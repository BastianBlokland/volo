#include "ai_result.h"
#include "core_annotation.h"
#include "core_array.h"

String ai_result_str(const AiResult res) {
  static const String g_names[] = {
      string_static("Running"),
      string_static("Success"),
      string_static("Failure"),
  };
  ASSERT(array_elems(g_names) == 3, "Incorrect number of names");
  return g_names[res];
}
