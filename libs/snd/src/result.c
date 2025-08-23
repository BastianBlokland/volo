#include "core/array.h"
#include "core/forward.h"
#include "core/string.h"
#include "snd/result.h"

String snd_result_str(const SndResult result) {
  static const String g_msgs[] = {
      string_static("Success"),
      string_static("FailedToAcquireObject"),
      string_static("InvalidObject"),
      string_static("InvalidObjectPhase"),
      string_static("ObjectStopped"),
      string_static("ParameterOutOfRange"),
  };
  ASSERT(array_elems(g_msgs) == SndResult_Count, "Incorrect number of result messages");
  return g_msgs[result];
}
