#include "core.h"
#include "core_array.h"
#include "snd_channel.h"

String snd_channel_str(const SndChannel channel) {
  static const String g_msgs[] = {
      string_static("Left"),
      string_static("Right"),
  };
  ASSERT(array_elems(g_msgs) == SndChannel_Count, "Incorrect number of channel messages");
  return g_msgs[channel];
}
