#include "check_spec.h"
#include "core_array.h"
#include "core_float.h"

#ifdef VOLO_SIMD
#include "core_simd.h"

spec(simd) {

  it("can sum components") {
    check_eq_float(simd_vec_x(simd_vec_add_comp(simd_vec_zero())), 0, 1e-8f);
    check_eq_float(simd_vec_x(simd_vec_add_comp(simd_vec_set(-1, 4, 6, 42))), 51, 1e-8f);
  }

  it("can find the min component") {
    check_eq_float(simd_vec_x(simd_vec_min_comp(simd_vec_set(-1, 4, 6, -42))), -42, 1e-8f);
    check_eq_float(simd_vec_x(simd_vec_min_comp(simd_vec_set(-1, 4, -42, 6))), -42, 1e-8f);
    check_eq_float(simd_vec_x(simd_vec_min_comp(simd_vec_set(-1, -42, 4, 6))), -42, 1e-8f);
    check_eq_float(simd_vec_x(simd_vec_min_comp(simd_vec_set(-42, -1, 4, 6))), -42, 1e-8f);
  }

  it("can find the min component of the first three") {
    check_eq_float(simd_vec_x(simd_vec_min_comp3(simd_vec_set(-1, 4, 6, -42))), -1, 1e-8f);
    check_eq_float(simd_vec_x(simd_vec_min_comp3(simd_vec_set(-1, 4, -42, 6))), -42, 1e-8f);
    check_eq_float(simd_vec_x(simd_vec_min_comp3(simd_vec_set(-1, -42, 4, 6))), -42, 1e-8f);
    check_eq_float(simd_vec_x(simd_vec_min_comp3(simd_vec_set(-42, -1, 4, -64))), -42, 1e-8f);
  }

  it("can find the max component") {
    check_eq_float(simd_vec_x(simd_vec_max_comp(simd_vec_set(-1, 4, 6, 42))), 42, 1e-8f);
    check_eq_float(simd_vec_x(simd_vec_max_comp(simd_vec_set(-1, 4, 42, 6))), 42, 1e-8f);
    check_eq_float(simd_vec_x(simd_vec_max_comp(simd_vec_set(-1, 42, 4, 6))), 42, 1e-8f);
    check_eq_float(simd_vec_x(simd_vec_max_comp(simd_vec_set(42, -1, 4, 6))), 42, 1e-8f);
  }

  it("can find the max component of the first three") {
    check_eq_float(simd_vec_x(simd_vec_max_comp3(simd_vec_set(-1, 4, 6, 42))), 6, 1e-8f);
    check_eq_float(simd_vec_x(simd_vec_max_comp3(simd_vec_set(-1, 4, 42, 6))), 42, 1e-8f);
    check_eq_float(simd_vec_x(simd_vec_max_comp3(simd_vec_set(-1, 42, 4, 64))), 42, 1e-8f);
    check_eq_float(simd_vec_x(simd_vec_max_comp3(simd_vec_set(42, -1, 4, 6))), 42, 1e-8f);
  }

  it("can convert f32 to f16") {
    struct {
      ALIGNAS(16) f32 in[4];
      f32 out[4];
      f32 threshold;
    } const data[] = {
        {{0.0f, 0.0f, 0.0f, 0.0f}, .out = {0.0f, 0.0f, 0.0f, 0.0f}},
        {{-1, 1, 1023, -1023}, .out = {-1, 1, 1023, -1023}},
        {{42, 13.33f, -1.337f, 1.337f}, .out = {42, 13.33f, -1.337f, 1.337f}, .threshold = 1e-2f},
    };

    for (usize i = 0; i != array_elems(data); ++i) {
      const SimdVec vecIn      = simd_vec_load(data[i].in);
      const SimdVec vecF16     = simd_vec_f32_to_f16_soft(vecIn);
      const u64     vecF16Data = simd_vec_u64(vecF16);
      for (u32 comp = 0; comp != 4; ++comp) {
        const f16 valF16 = vecF16Data >> (comp * 16);
        const f32 valF32 = float_f16_to_f32(valF16);
        if (data[i].threshold == 0.0f) {
          check_msg(
              valF32 == data[i].out[comp],
              "{}[{}] {} == {}",
              fmt_int(i),
              fmt_int(comp),
              fmt_float(valF32),
              fmt_float(data[i].out[comp]));
        } else {
          check_eq_float(valF32, data[i].out[comp], data[i].threshold);
        }
      }
    }
  }
}
#endif
