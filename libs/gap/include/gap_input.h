#pragma once
#include "core_string.h"

/**
 * Represents a physical key (independent of the users keyboard layout).
 * NOTE: Care must be taken when changing these values, they are potentially serialized.
 */
typedef enum {
  GapKey_None      = -1,
  GapKey_MouseLeft = 0,
  GapKey_MouseRight,
  GapKey_MouseMiddle,

  GapKey_Shift,
  GapKey_Control,
  GapKey_Backspace,
  GapKey_Delete,
  GapKey_Tab,
  GapKey_Tilde,
  GapKey_Return,
  GapKey_Escape,
  GapKey_Space,
  GapKey_Plus,
  GapKey_Minus,
  GapKey_Home,
  GapKey_End,
  GapKey_PageUp,
  GapKey_PageDown,
  GapKey_ArrowUp,
  GapKey_ArrowDown,
  GapKey_ArrowRight,
  GapKey_ArrowLeft,

  GapKey_A,
  GapKey_B,
  GapKey_C,
  GapKey_D,
  GapKey_E,
  GapKey_F,
  GapKey_G,
  GapKey_H,
  GapKey_I,
  GapKey_J,
  GapKey_K,
  GapKey_L,
  GapKey_M,
  GapKey_N,
  GapKey_O,
  GapKey_P,
  GapKey_Q,
  GapKey_R,
  GapKey_S,
  GapKey_T,
  GapKey_U,
  GapKey_V,
  GapKey_W,
  GapKey_X,
  GapKey_Y,
  GapKey_Z,

  GapKey_Alpha0,
  GapKey_Alpha1,
  GapKey_Alpha2,
  GapKey_Alpha3,
  GapKey_Alpha4,
  GapKey_Alpha5,
  GapKey_Alpha6,
  GapKey_Alpha7,
  GapKey_Alpha8,
  GapKey_Alpha9,

  GapKey_F1,
  GapKey_F2,
  GapKey_F3,
  GapKey_F4,
  GapKey_F5,
  GapKey_F6,
  GapKey_F7,
  GapKey_F8,
  GapKey_F9,
  GapKey_F10,
  GapKey_F11,
  GapKey_F12,

  GapKey_Count,
} GapKey;

typedef enum {
  GapParam_None       = -1,
  GapParam_WindowSize = 0,
  GapParam_CursorPos,
  GapParam_CursorDelta,
  GapParam_ScrollDelta,

  GapParam_Count,
} GapParam;

/**
 * Textual representation of a key.
 */
String gap_key_str(GapKey);

/**
 * Textual representation of a parameter.
 */
String gap_param_str(GapParam);
