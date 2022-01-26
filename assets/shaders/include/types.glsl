#ifndef INCLUDE_TYPES
#define INCLUDE_TYPES

#extension GL_EXT_shader_16bit_storage : enable

#define i16 int16_t
#define i16_vec2 i16vec2
#define i16_vec3 i16vec3
#define i16_vec4 i16vec4

#define u16 uint16_t
#define u16_vec2 u16vec2
#define u16_vec3 u16vec3
#define u16_vec4 u16vec4

#define i32 int
#define i32_vec2 ivec2
#define i32_vec3 ivec3
#define i32_vec4 ivec4

#define u32 uint
#define u32_vec2 uvec2
#define u32_vec3 uvec3
#define u32_vec4 uvec4

#define f16 float16_t
#define f16_vec2 f16vec2
#define f16_vec3 f16vec3
#define f16_vec4 f16vec4

#define f32 float
#define f32_vec2 vec2
#define f32_vec3 vec3
#define f32_vec4 vec4
#define f32_mat3 mat3
#define f32_mat4 mat4

#define f64 double
#define f64_vec2 dvec2
#define f64_vec3 dvec3
#define f64_vec4 dvec4

#endif // INCLUDE_TYPES
