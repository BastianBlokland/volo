#include "ai_value.h"
#include "core_array.h"

String ai_value_type_str(const AiValueType type) {
  static const String g_names[] = {
      string_static("f64"),
      string_static("bool"),
      string_static("vector"),
      string_static("time"),
      string_static("entity"),
  };
  ASSERT(array_elems(g_names) == AiValueType_Count, "Incorrect number of names");
  return g_names[type];
}
