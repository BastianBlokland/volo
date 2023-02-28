#ifndef INCLUDE_BINDING
#define INCLUDE_BINDING

#include "types.glsl"

/**
 * Set indices.
 */
const u32 c_setGlobal   = 0;
const u32 c_setGraphic  = 1;
const u32 c_setDynamic  = 2;
const u32 c_setDraw     = 3;
const u32 c_setInstance = 4;

/**
 * Maximum count of bindings per type for each set.
 */
const u32 c_setGlobalMaxData   = 1;
const u32 c_setGlobalMaxImage  = 5;
const u32 c_setGraphicMaxData  = 1;
const u32 c_setGraphicMaxImage = 6;

/**
 * Declare a global (per pass) binding.
 */
#define bind_global_data(_IDX_) layout(set = c_setGlobal, binding = _IDX_, std140)
#define bind_global_img(_IDX_) layout(set = c_setGlobal, binding = c_setGlobalMaxData + _IDX_)

/**
 * Declare a per-graphic binding.
 */
#define bind_graphic_data(_IDX) layout(set = c_setGraphic, binding = _IDX, std140)
#define bind_graphic_img(_IDX) layout(set = c_setGraphic, binding = c_setGraphicMaxData + _IDX)

/**
 * Declare a dynamic binding.
 * Allows for binding resources (like meshes) dynamically instead of fixed per graphic.
 *
 * Supported indices:
 *  0: Mesh (Storage buffer).
 */
#define bind_dynamic(_BIND_IDX_) layout(set = c_setDynamic, binding = _BIND_IDX_)
#define bind_dynamic_data(_BIND_IDX_) layout(set = c_setDynamic, binding = _BIND_IDX_, std140)

/**
 * Declare a per-draw binding.
 * Supported indices:
 *  0: User data (Uniform buffer).
 */
#define bind_draw(_BIND_IDX_) layout(set = c_setDraw, binding = _BIND_IDX_)
#define bind_draw_data(_BIND_IDX_) layout(set = c_setDraw, binding = _BIND_IDX_, std140)

/**
 * Declare a per-instance binding.
 * Supported indices:
 *  0: User data (Uniform buffer).
 */
#define bind_instance(_BIND_IDX_) layout(set = c_setInstance, binding = _BIND_IDX_)
#define bind_instance_data(_BIND_IDX_) layout(set = c_setInstance, binding = _BIND_IDX_, std140)

/**
 * Declare an internal (for example vertex to fragment) binding.
 */
#define bind_internal(_BIND_IDX_) layout(location = _BIND_IDX_)

/**
 * Declare a specialization constant binding.
 */
#define bind_spec(_BIND_IDX_) layout(constant_id = _BIND_IDX_)

/**
 * Build-in bindings.
 */
#define in_vertexIndex gl_VertexIndex
#define in_instanceIndex gl_InstanceIndex
#define in_fragCoord gl_FragCoord
#define out_vertexPosition gl_Position
#define out_pointSize gl_PointSize

#endif // INCLUDE_BINDING
