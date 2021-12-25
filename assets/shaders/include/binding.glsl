#ifndef INCLUDE_BINDING
#define INCLUDE_BINDING

#include "types.glsl"

const u32 g_setGlobal  = 0;
const u32 g_setGraphic = 1;

#define bind_global(_BIND_IDX_) layout(set = g_setGlobal, binding = _BIND_IDX_)
#define bind_graphic(_BIND_IDX_) layout(set = g_setGraphic, binding = _BIND_IDX_)

#define bind_global_align(_BIND_IDX_) layout(set = g_setGlobal, binding = _BIND_IDX_, std140)
#define bind_graphic_align(_BIND_IDX_) layout(set = g_setGraphic, binding = _BIND_IDX_, std140)

#endif // INCLUDE_BINDING
