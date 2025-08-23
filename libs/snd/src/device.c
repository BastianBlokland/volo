#include "core/array.h"

#include "device.h"

String snd_device_state_str(const SndDeviceState state) {
  static const String g_msgs[] = {
      string_static("Error"),
      string_static("Idle"),
      string_static("Playing"),
  };
  ASSERT(array_elems(g_msgs) == SndDeviceState_Count, "Incorrect number of state messages");
  return g_msgs[state];
}
