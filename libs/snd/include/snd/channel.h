#pragma once
#include "core/forward.h"

typedef enum eSndChannel {
  SndChannel_Left,
  SndChannel_Right,

  SndChannel_Count,
} SndChannel;

String snd_channel_str(SndChannel);
