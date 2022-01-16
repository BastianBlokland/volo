#ifndef INCLUDE_BINDING
#define INCLUDE_BINDING

#include "types.glsl"

const u32 c_setGlobal   = 0;
const u32 c_setGraphic  = 1;
const u32 c_setInstance = 2;

#define bind_global(_BIND_IDX_) layout(set = c_setGlobal, binding = _BIND_IDX_)
#define bind_global_data(_BIND_IDX_) layout(set = c_setGlobal, binding = _BIND_IDX_, std140)

#define bind_graphic(_BIND_IDX_) layout(set = c_setGraphic, binding = _BIND_IDX_)
#define bind_graphic_data(_BIND_IDX_) layout(set = c_setGraphic, binding = _BIND_IDX_, std140)

#define bind_instance(_BIND_IDX_) layout(set = c_setInstance, binding = _BIND_IDX_)
#define bind_instance_data(_BIND_IDX_) layout(set = c_setInstance, binding = _BIND_IDX_, std140)

#define bind_internal(_BIND_IDX_) layout(location = _BIND_IDX_)

#define bind_spec(_BIND_IDX_) layout(constant_id = _BIND_IDX_)

/**
 * Build-in bindings.
 */
#define in_vertexIndex gl_VertexIndex
#define in_instanceIndex gl_InstanceIndex
#define out_vertexPosition gl_Position
#define out_pointSize gl_PointSize

#endif // INCLUDE_BINDING
