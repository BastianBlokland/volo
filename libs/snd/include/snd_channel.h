#pragma once
#include "core_string.h"

typedef enum {
  SndChannel_Left,
  SndChannel_Right,

  SndChannel_Count,
} SndChannel;

String snd_channel_str(SndChannel);
