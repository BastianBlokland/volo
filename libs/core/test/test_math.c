#include "check_spec.h"
#include "core_math.h"

spec(math) {

  it("can compute the min argument") {
    check_eq_int(math_min(1, 0), 0);
    check_eq_int(math_min(0, 0), 0);
    check_eq_int(math_min(1, -1), -1);
    check_eq_int(math_min(-1, 0), -1);

    check_eq_float(math_min(-1.0f, 0.0f), -1.0f, 1e-6);
    check_eq_float(math_min(-1.1f, -1.2f), -1.2f, 1e-6);
  }

  it("can compute the max argument") {
    check_eq_int(math_max(1, 0), 1);
    check_eq_int(math_max(0, 0), 0);
    check_eq_int(math_max(-1, 1), 1);
    check_eq_int(math_max(-1, -2), -1);

    check_eq_float(math_max(-1.0f, 0.1f), 0.1f, 1e-6);
    check_eq_float(math_max(-1.1f, -1.2f), -1.1f, 1e-6);
  }

  it("can compute the sign of the argument") {
    check_eq_int(math_sign(-42), -1);
    check_eq_int(math_sign(42), 1);
    check_eq_int(math_sign(0), 0);

    check_eq_int(math_sign(-0.1f), -1);
    check_eq_int(math_sign(0.1f), 1);
    check_eq_int(math_sign(0.0f), 0);
  }

  it("can compute the absolute of the argument") {
    check_eq_int(math_abs(-42), 42);
    check_eq_int(math_abs(42), 42);
    check_eq_int(math_abs(0), 0);
    check_eq_float(math_abs(-1.25), 1.25, 1e-6);
    check_eq_float(math_abs(0.0), 0.0, 1e-6);
  }

  it("can linearly interpolate values") {
    check_eq_int(math_lerp(42, 1337, 0), 42);
    check_eq_int(math_lerp(-42, 1337, 0), -42);
    check_eq_int(math_lerp(1337, 42, 0), 1337);
    check_eq_int(math_lerp(42, 1337, 1), 1337);
    check_eq_int(math_lerp(42, -1337, 1), -1337);
    check_eq_int(math_lerp(1337, 42, 1), 42);
    check_eq_int(math_lerp(10, 20, 2), 30);
    check_eq_int(math_lerp(-10, -20, 2), -30);
  }

  it("can linearly un-interpolate values") {
    check_eq_float(math_unlerp(10, 20, 10), 0, 1e-12);
    check_eq_float(math_unlerp(-10, -20, -10), 0, 1e-12);

    check_eq_float(math_unlerp(10, 20, 20), 1, 1e-12);
    check_eq_float(math_unlerp(-10, -20, -20), 1, 1e-12);

    check_eq_float(math_unlerp(10, 20, 30), 2, 1e-12);
    check_eq_float(math_unlerp(-10, -20, -30), 2, 1e-12);

    const f32 x   = 42.1337f;
    const f32 y   = -47.3f;
    const f32 t   = -3.153f;
    const f32 val = math_lerp(x, y, t);
    check_eq_float(math_unlerp(x, y, val), t, 1e-12);
  }

  it("can truncate the fractional part of floats") {
    check_eq_float(math_trunc_f64(1.42), 1.0, 1e-24);
    check_eq_float(math_trunc_f64(42.1337), 42.0, 1e-24);
    check_eq_float(math_trunc_f64(-1.42), -1.0, 1e-24);
    check_eq_float(math_trunc_f64(-42.1337), -42.0, 1e-24);
    check_eq_float(math_trunc_f64(-.34), 0.0, 1e-24);
  }

  it("can floor (round-down) floats") {
    check_eq_float(math_floor_f64(.1), 0.0, 1e-24);
    check_eq_float(math_floor_f64(1.1), 1.0, 1e-24);
    check_eq_float(math_floor_f64(1.99), 1.0, 1e-24);
    check_eq_float(math_floor_f64(-42.1337), -43.0, 1e-24);
    check_eq_float(math_floor_f64(-2.3), -3.0, 1e-24);
    check_eq_float(math_floor_f64(-1.99), -2.0, 1e-24);
  }

  it("can ceil (round-up) floats") {
    check_eq_float(math_ceil_f64(1.0), 1.0, 1e-24);
    check_eq_float(math_ceil_f64(0.0), 0.0, 1e-24);
    check_eq_float(math_ceil_f64(1.2), 2.0, 1e-24);
    check_eq_float(math_ceil_f64(-1.0), -1.0, 1e-24);
    check_eq_float(math_ceil_f64(-1.2), -1.0, 1e-24);
    check_eq_float(math_ceil_f64(-42.1337), -42.0, 1e-24);
    check_eq_float(math_ceil_f64(-1.99), -1.0, 1e-24);
    check_eq_float(math_ceil_f64(-1.01), -1.0, 1e-24);
  }

  it("can round (round to even) floats") {
    check_eq_float(math_round_f64(1.0), 1.0, 1e-24);
    check_eq_float(math_round_f64(.0), 0.0, 1e-24);
    check_eq_float(math_round_f64(.6), 1.0, 1e-24);
    check_eq_float(math_round_f64(.5), 0.0, 1e-24);
    check_eq_float(math_round_f64(.499), 0.0, 1e-24);
    check_eq_float(math_round_f64(.51), 1.0, 1e-24);
    check_eq_float(math_round_f64(1.4), 1.0, 1e-24);
    check_eq_float(math_round_f64(1.5), 2.0, 1e-24);
    check_eq_float(math_round_f64(1.6), 2.0, 1e-24);
    check_eq_float(math_round_f64(2.5), 2.0, 1e-24);
    check_eq_float(math_round_f64(2.6), 3.0, 1e-24);
    check_eq_float(math_round_f64(3.5), 4.0, 1e-24);
    check_eq_float(math_round_f64(-.1), 0.0, 1e-24);
    check_eq_float(math_round_f64(-.4), 0.0, 1e-24);
    check_eq_float(math_round_f64(-.5), 0.0, 1e-24);
    check_eq_float(math_round_f64(-1.5), -2.0, 1e-24);
    check_eq_float(math_round_f64(-1.6), -2.0, 1e-24);
    check_eq_float(math_round_f64(-2.5), -2.0, 1e-24);
    check_eq_float(math_round_f64(-2.6), -3.0, 1e-24);
    check_eq_float(math_round_f64(-3.5), -4.0, 1e-24);
  }

  it("can clamp 32 bit floats") {
    check_eq_float(math_clamp_f32(0, 0, 1), 0, 1e-12);
    check_eq_float(math_clamp_f32(-2, 0, 1), 0, 1e-12);
    check_eq_float(math_clamp_f32(-.1, 0, 1), 0, 1e-12);
    check_eq_float(math_clamp_f32(1.1, 0, 1), 1, 1e-12);
    check_eq_float(math_clamp_f32(2, 0, 1), 1, 1e-12);
    check_eq_float(math_clamp_f32(-6, -5, -3), -5, 1e-12);
    check_eq_float(math_clamp_f32(-2, -5, -3), -3, 1e-12);
  }
}
