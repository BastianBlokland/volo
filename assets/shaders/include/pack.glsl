#ifndef INCLUDE_PACK
#define INCLUDE_PACK

#include "types.glsl"

u32 pack_u16_from_f32_bottom(const f32 val) {
  return floatBitsToUint(val) & 0xFFFF;
}

u32 pack_u16_from_f32_top(const f32 val) {
  return (floatBitsToUint(val) >> 16) & 0xFFFF;
}

f32 pack_frac8_from_u16_bottom(const u32 val) {
  return f32(val & 0xFF) / f32(0xFF);
}

f32 pack_frac8_from_u16_top(const u32 val) {
  return f32((val >> 8) & 0xFF) / f32(0xFF);
}

#endif // INCLUDE_PACK
