#ifndef INCLUDE_BINDING
#define INCLUDE_BINDING

#include "types.glsl"

/**
 * Set indices.
 */
const u32 c_setGlobal   = 0;
const u32 c_setDraw     = 1;
const u32 c_setGraphic  = 2;
const u32 c_setInstance = 3;

/**
 * Maximum count of bindings per type for each set.
 */
const u32 c_setGlobalMaxData   = 1;
const u32 c_setGlobalMaxImage  = 5;
const u32 c_setDrawMaxData     = 2;
const u32 c_setDrawMaxImage    = 1;
const u32 c_setGraphicMaxData  = 1;
const u32 c_setGraphicMaxImage = 6;
const u32 c_setInstanceMaxData = 1;

/**
 * Declare a global (per pass) binding.
 */
#define bind_global_data(_IDX_) layout(set = c_setGlobal, binding = _IDX_, std140)
#define bind_global_img(_IDX_) layout(set = c_setGlobal, binding = c_setGlobalMaxData + _IDX_)

/**
 * Declare a per-draw binding.
 * Allows for binding resources (like meshes) per draw instead of fixed per graphic.
 */
#define bind_draw_data(_IDX_) layout(set = c_setDraw, binding = _IDX_, std140)
#define bind_draw_img(_IDX_) layout(set = c_setDraw, binding = c_setDrawMaxData + _IDX_)

/**
 * Declare a per-graphic binding.
 */
#define bind_graphic_data(_IDX) layout(set = c_setGraphic, binding = _IDX, std140)
#define bind_graphic_img(_IDX) layout(set = c_setGraphic, binding = c_setGraphicMaxData + _IDX)

/**
 * Declare a per-instance binding.
 */
#define bind_instance_data(_IDX_) layout(set = c_setInstance, binding = _IDX_, std140)

/**
 * Declare an internal (for example vertex to fragment) binding.
 */
#define bind_internal(_IDX_) layout(location = _IDX_)

/**
 * Declare a specialization constant binding.
 */
#define bind_spec(_IDX_) layout(constant_id = _IDX_)

/**
 * Build-in bindings.
 */
#define in_vertexIndex gl_VertexIndex
#define in_instanceIndex gl_InstanceIndex
#define in_fragCoord gl_FragCoord
#define out_vertexPosition gl_Position
#define out_pointSize gl_PointSize

#endif // INCLUDE_BINDING
