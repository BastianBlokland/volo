#include "core_array.h"

#include "device_internal.h"

String snd_device_status_str(const SndDeviceStatus status) {
  static const String g_msgs[] = {
      string_static("Ready"),
      string_static("FrameActive"),
      string_static("InitFailed"),
  };
  ASSERT(array_elems(g_msgs) == SndDeviceStatus_Count, "Incorrect number of status messages");
  return g_msgs[status];
}
