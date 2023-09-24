#pragma once
#include "core_string.h"
#include "core_unicode.h"

#define UI_SHAPES                                                                                  \
  X(0x0020, Empty)                                                                                 \
  X(0xE034, Pause)                                                                                 \
  X(0xE04F, VolumeOff)                                                                             \
  X(0xE050, VolumeUp)                                                                              \
  X(0xE069, WebAsset)                                                                              \
  X(0xE145, Add)                                                                                   \
  X(0xE161, Save)                                                                                  \
  X(0xE162, SelectAll)                                                                             \
  X(0xE1DB, Storage)                                                                               \
  X(0xE25E, FormatShapes)                                                                          \
  X(0xE312, Keyboard)                                                                              \
  X(0xE322, Memory)                                                                                \
  X(0xE338, VideogameAsset)                                                                        \
  X(0xE3AE, Brush)                                                                                 \
  X(0xE3EA, Grain)                                                                                 \
  X(0xE3F4, Image)                                                                                 \
  X(0xE405, MusicNote)                                                                             \
  X(0xE412, PhotoCamera)                                                                           \
  X(0xE425, Timer)                                                                                 \
  X(0xE429, Tune)                                                                                  \
  X(0xE4FC, QueryStats)                                                                            \
  X(0xE518, Light)                                                                                 \
  X(0xE574, Category)                                                                              \
  X(0xE5CA, Check)                                                                                 \
  X(0xE5CD, Close)                                                                                 \
  X(0xE5CE, ExpandLess)                                                                            \
  X(0xE5CF, ExpandMore)                                                                            \
  X(0xE5D0, Fullscreen)                                                                            \
  X(0xE5D5, Refresh)                                                                               \
  X(0xE5D6, UnfoldLess)                                                                            \
  X(0xE5D7, UnfoldMore)                                                                            \
  X(0xE71C, Animation)                                                                             \
  X(0xE7EF, Group)                                                                                 \
  X(0xE80B, Globe)                                                                                 \
  X(0xE80E, Whatshot)                                                                              \
  X(0xE868, Bug)                                                                                   \
  X(0xE873, Description)                                                                           \
  X(0xE87B, Extension)                                                                             \
  X(0xE89E, OpenInNew)                                                                             \
  X(0xE8B8, Settings)                                                                              \
  X(0xE8F4, Visibility)                                                                            \
  X(0xE92B, Delete)                                                                                \
  X(0xE9BA, Logout)                                                                                \
  X(0xE9E4, Speed)                                                                                 \
  X(0xE9FE, ViewInAr)                                                                              \
  X(0xEA3C, Construction)                                                                          \
  X(0xEA4A, Psychology)                                                                            \
  X(0xEA5F, Calculate)                                                                             \
  X(0xEF5B, Monitor)                                                                               \
  X(0xF000, Square)                                                                                \
  X(0xF001, Circle)                                                                                \
  X(0xF002, CursorVerticalBar)                                                                     \
  X(0xF003, Triangle)                                                                              \
  X(0xF016, Grid4x4)                                                                               \
  X(0xF053, Restart)                                                                               \
  X(0xF233, Groups)                                                                                \
  X(0xF230, Default)

enum {
#define X(_UNICODE_, _NAME_) UiShape_##_NAME_ = _UNICODE_,
  UI_SHAPES
#undef X
};

#define fmt_ui_shape(_SHAPE_) fmt_text(ui_shape_scratch(UiShape_##_SHAPE_))

String ui_shape_scratch(Unicode);
