#include "core_math.h"

#include "types_internal.h"

RvkSize rvk_size_scale(const RvkSize size, const f32 scale) {
  return rvk_size(
      (u16)math_round_nearest_f32(size.width * scale),
      (u16)math_round_nearest_f32(size.height * scale));
}
