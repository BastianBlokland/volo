#include "core.h"
#include "core_array.h"
#include "snd_result.h"

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
