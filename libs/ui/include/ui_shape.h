#pragma once
#include "core_format.h"
#include "core_unicode.h"

#define UI_SHAPES                                                                                  \
  X(0xF000, Square)                                                                                \
  X(0xF001, Circle)                                                                                \
  X(0xE5CA, Check)                                                                                 \
  X(0xE9E4, Speed)                                                                                 \
  X(0xE9BA, Logout)                                                                                \
  X(0xE5D0, Fullscreen)                                                                            \
  X(0xE5D1, FullscreenExit)                                                                        \
  X(0xE89E, OpenInNew)                                                                             \
  X(0xE53B, Layers)                                                                                \
  X(0xE53C, LayersClear)                                                                           \
  X(0xEF5B, Monitor)                                                                               \
  X(0xE3AE, Brush)                                                                                 \
  X(0xE1DB, Storage)                                                                               \
  X(0xEA5F, Calculate)                                                                             \
  X(0xE069, WebAsset)                                                                              \
  X(0xE338, VideogameAsset)                                                                        \
  X(0xE4FC, QueryStats)                                                                            \
  X(0xE80E, Whatshot)                                                                              \
  X(0xE312, Keyboard)                                                                              \
  X(0xE5CD, Close)

enum {
#define X(_UNICODE_, _NAME_) UiShape_##_NAME_ = _UNICODE_,
  UI_SHAPES
#undef X
};

#define fmt_ui_shape(_SHAPE_) fmt_text(ui_shape_scratch(UiShape_##_SHAPE_))

String ui_shape_scratch(Unicode);
