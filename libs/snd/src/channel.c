#include "core/array.h"
#include "core/forward.h"
#include "core/string.h"
#include "snd/channel.h"

String snd_channel_str(const SndChannel channel) {
  static const String g_msgs[] = {
      string_static("Left"),
      string_static("Right"),
  };
  ASSERT(array_elems(g_msgs) == SndChannel_Count, "Incorrect number of channel messages");
  return g_msgs[channel];
}
