#include "check_spec.h"
#include "core_alloc.h"
#include "core_float.h"
#include "core_rng.h"
#include "geo_color.h"

#include "utils_internal.h"

spec(color) {

  it("sums all components when adding") {
    check_eq_color(
        geo_color_add(geo_color(1, -2.1f, 3, 4), geo_color(2, 3.2f, 4, 5)),
        geo_color(3, 1.1f, 7, 9));
    check_eq_color(
        geo_color_add(geo_color(1, 2, 3, 0), geo_color(4, 5, 6, 0)), geo_color(5, 7, 9, 0));
  }

  it("multiplies each component by the scalar when multiplying") {
    check_eq_color(geo_color_mul(geo_color(5, -2.1f, 6, 8), 2), geo_color(10, -4.2f, 12, 16));
    check_eq_color(geo_color_mul(geo_color(1, 2, 3, 0), -2), geo_color(-2, -4, -6, 0));
  }

  it("multiplies each component when multiplying component-wise") {
    const GeoColor v1 = {.r = 10, .g = 20, .b = 10, .a = 2};
    const GeoColor v2 = {.r = 2, .g = 3, .b = -4, .a = 0};
    check_eq_color(geo_color_mul_comps(v1, v2), geo_color(20, 60, -40, 0));
  }

  it("divides each component by the scalar when dividing") {
    check_eq_color(geo_color_div(geo_color(5, -2.1f, 6, 8), 2), geo_color(2.5, -1.05f, 3, 4));
    check_eq_color(geo_color_div(geo_color(1, 2, 3, 1), -2), geo_color(-.5, -1, -1.5, -0.5f));
  }

  it("multiplies each component when dividing component-wise") {
    const GeoColor v1 = {.r = 20, .g = 60, .b = 10, .a = 2};
    const GeoColor v2 = {.r = 2, .g = 3, .b = -4, .a = 1};
    check_eq_color(geo_color_div_comps(v1, v2), geo_color(10, 20, -2.5f, 2));
  }

  it("can linearly interpolate colors") {
    const GeoColor c1 = geo_color(10, 20, 10, 1);
    const GeoColor c2 = geo_color(20, 40, 20, 1);
    const GeoColor c3 = geo_color(15, 30, 15, 1);
    check_eq_color(geo_color_lerp(c1, c2, .5f), c3);
  }

  it("can bilinearly interpolate colors") {
    const GeoColor c1 = geo_color(1, 2, 3, 4);
    const GeoColor c2 = geo_color(5, 6, 7, 8);
    const GeoColor c3 = geo_color(9, 10, 11, 12);
    const GeoColor c4 = geo_color(13, 14, 15, 16);

    check_eq_color(geo_color_bilerp(c1, c2, c3, c4, 0, 0), c1);
    check_eq_color(geo_color_bilerp(c1, c2, c3, c4, 1, 0), c2);
    check_eq_color(geo_color_bilerp(c1, c2, c3, c4, 0, 1), c3);
    check_eq_color(geo_color_bilerp(c1, c2, c3, c4, 1, 1), c4);
    check_eq_color(geo_color_bilerp(c1, c2, c3, c4, 0.5, 0.5), geo_color(7, 8, 9, 10));
    check_eq_color(geo_color_bilerp(c1, c2, c3, c4, 0.5, 0), geo_color(3, 4, 5, 6));
    check_eq_color(geo_color_bilerp(c1, c2, c3, c4, 0.5, 1), geo_color(11, 12, 13, 14));
    check_eq_color(geo_color_bilerp(c1, c2, c3, c4, 0, 0.5), geo_color(5, 6, 7, 8));
    check_eq_color(geo_color_bilerp(c1, c2, c3, c4, 1, 0.5), geo_color(9, 10, 11, 12));
  }

  it("can compute the minimum value of each component") {
    const GeoColor c1 = {.r = 2, .g = 6, .b = -5, .a = 5};
    const GeoColor c2 = {.r = 4, .g = -2, .b = 6, .a = 5};
    check_eq_color(geo_color_min(c1, c2), geo_color(2, -2, -5, 5));
  }

  it("can compute the maximum value of each component") {
    const GeoColor c1 = {.r = 2, .g = 6, .b = -5, .a = 5};
    const GeoColor v2 = {.r = 4, .g = -2, .b = 6, .a = 5};
    check_eq_color(geo_color_max(c1, v2), geo_color(4, 6, 6, 5));
  }

  it("can clamp its magnitude") {
    check_eq_color(geo_color_clamp(geo_color(1, 2, 3, 0), 10), geo_color(1, 2, 3, 0));
    check_eq_color(geo_color_clamp(geo_color(34, 0, 0, 0), 2), geo_color(2, 0, 0, 0));
    check_eq_color(geo_color_clamp(geo_color(1, 2, 3, 0), 0), geo_color(0, 0, 0, 0));
    check_eq_color(geo_color_clamp(geo_color(0, 0, 0, 0), 0), geo_color(0, 0, 0, 0));
  }

  it("can clamp components") {
    const GeoColor c    = {.r = -1, .g = 0, .b = 1, .a = 2};
    const GeoColor cMin = {.r = 2, .g = -1, .b = 3, .a = 1};
    const GeoColor cMax = {.r = 3, .g = 1, .b = 4, .a = 1};
    check_eq_color(geo_color_clamp_comps(c, cMin, cMax), geo_color(2, 0, 3, 1));
  }

  it("can clamp between 0 and 1 (saturate)") {
    const GeoColor c = {.r = -1, .g = 0.5f, .b = 1, .a = 2};
    check_eq_color(geo_color_clamp01(c), geo_color(0, 0.5f, 1, 1));
  }

  it("lists all components when formatted") {
    check_eq_string(
        fmt_write_scratch("{}", geo_color_fmt(geo_color_white)), string_lit("1, 1, 1, 1"));
    check_eq_string(
        fmt_write_scratch("{}", geo_color_fmt(geo_color_red)), string_lit("1, 0, 0, 1"));
    check_eq_string(
        fmt_write_scratch("{}", geo_color_fmt(geo_color(42, 1337, 1, 0.42f))),
        string_lit("42, 1337, 1, 0.42"));
  }

  it("can create a color from hsv") {
    check_eq_color(geo_color_from_hsv(0.0f, 0.0f, 0.0f, 1.0f), geo_color_black);
    check_eq_color(geo_color_from_hsv(0.0f, 0.0f, 1.0f, 1.0f), geo_color_white);
    check_eq_color(geo_color_from_hsv(0.0f, 0.0f, 0.5f, 1.0f), geo_color(0.5f, 0.5f, 0.5f, 1.0f));
    check_eq_color(geo_color_from_hsv(0.0f, 1.0f, 1.0f, 1.0f), geo_color(1.0f, 0.0f, 0.0f, 1.0f));
    check_eq_color(geo_color_from_hsv(0.0f, 1.0f, 1.0f, 1.0f), geo_color(1.0f, 0.0f, 0.0f, 1.0f));
    check_eq_color(geo_color_from_hsv(1.0f, 1.0f, 1.0f, 1.0f), geo_color(1.0f, 0.0f, 0.0f, 1.0f));
    check_eq_color(geo_color_from_hsv(0.5f, 1.0f, 1.0f, 1.0f), geo_color(0.0f, 1.0f, 1.0f, 1.0f));
    check_eq_color(geo_color_from_hsv(0.25f, 1.0f, 1.0f, 1.0f), geo_color(0.5f, 1.0f, 0.0f, 1.0f));
    check_eq_color(geo_color_from_hsv(0.75f, 1.0f, 1.0f, 1.0f), geo_color(0.5f, 0.0f, 1.0f, 1.0f));
    check_eq_color(geo_color_from_hsv(0.0f, 0.5f, 1.0f, 1.0f), geo_color(1.0f, 0.5f, 0.5f, 1.0f));
    check_eq_color(geo_color_from_hsv(1.0f, 0.5f, 1.0f, 1.0f), geo_color(1.0f, 0.5f, 0.5f, 1.0f));
    check_eq_color(geo_color_from_hsv(0.5f, 0.5f, 1.0f, 1.0f), geo_color(0.5f, 1.0f, 1.0f, 1.0f));
    check_eq_color(geo_color_from_hsv(0.25f, 0.5f, 1.0f, 1.0f), geo_color(0.75f, 1.0f, 0.5f, 1.0f));
    check_eq_color(geo_color_from_hsv(0.75f, 0.5f, 1.0f, 1.0f), geo_color(0.75f, 0.5f, 1.0f, 1.0f));
  }

  it("can convert a color to hsv") {
    {
      f32 hue, saturation, value, alpha;
      geo_color_to_hsv(geo_color_black, &hue, &saturation, &value, &alpha);
      check_eq_float(hue, 0.0f, 1e-8f);
      check_eq_float(saturation, 0.0f, 1e-8f);
      check_eq_float(value, 0.0f, 1e-8f);
      check_eq_float(alpha, 1.0f, 1e-8f);
    }
    {
      f32 hue, saturation, value, alpha;
      geo_color_to_hsv(geo_color_white, &hue, &saturation, &value, &alpha);
      check_eq_float(hue, 0.0f, 1e-8f);
      check_eq_float(saturation, 0.0f, 1e-8f);
      check_eq_float(value, 1.0f, 1e-8f);
      check_eq_float(alpha, 1.0f, 1e-8f);
    }
    {
      f32 hue, saturation, value, alpha;
      geo_color_to_hsv(geo_color(0.5f, 0.5f, 0.5f, 1.0f), &hue, &saturation, &value, &alpha);
      check_eq_float(hue, 0.0f, 1e-8f);
      check_eq_float(saturation, 0.0f, 1e-8f);
      check_eq_float(value, 0.5f, 1e-8f);
      check_eq_float(alpha, 1.0f, 1e-8f);
    }
    {
      f32 hue, saturation, value, alpha;
      geo_color_to_hsv(geo_color(1.0f, 0.0f, 0.0f, 1.0f), &hue, &saturation, &value, &alpha);
      check_eq_float(hue, 0.0f, 1e-8f);
      check_eq_float(saturation, 1.0f, 1e-8f);
      check_eq_float(value, 1.0f, 1e-8f);
      check_eq_float(alpha, 1.0f, 1e-8f);
    }
    {
      f32 hue, saturation, value, alpha;
      geo_color_to_hsv(geo_color(0.0f, 1.0f, 1.0f, 1.0f), &hue, &saturation, &value, &alpha);
      check_eq_float(hue, 0.5f, 1e-8f);
      check_eq_float(saturation, 1.0f, 1e-8f);
      check_eq_float(value, 1.0f, 1e-8f);
      check_eq_float(alpha, 1.0f, 1e-8f);
    }
    {
      f32 hue, saturation, value, alpha;
      geo_color_to_hsv(geo_color(0.5f, 1.0f, 0.0f, 1.0f), &hue, &saturation, &value, &alpha);
      check_eq_float(hue, 0.25f, 1e-8f);
      check_eq_float(saturation, 1.0f, 1e-8f);
      check_eq_float(value, 1.0f, 1e-8f);
      check_eq_float(alpha, 1.0f, 1e-8f);
    }
    {
      f32 hue, saturation, value, alpha;
      geo_color_to_hsv(geo_color(0.5f, 0.0f, 1.0f, 1.0f), &hue, &saturation, &value, &alpha);
      check_eq_float(hue, 0.75f, 1e-8f);
      check_eq_float(saturation, 1.0f, 1e-8f);
      check_eq_float(value, 1.0f, 1e-8f);
      check_eq_float(alpha, 1.0f, 1e-8f);
    }
    {
      f32 hue, saturation, value, alpha;
      geo_color_to_hsv(geo_color(1.0f, 0.5f, 0.5f, 1.0f), &hue, &saturation, &value, &alpha);
      check_eq_float(hue, 0.0f, 1e-8f);
      check_eq_float(saturation, 0.5f, 1e-8f);
      check_eq_float(value, 1.0f, 1e-8f);
      check_eq_float(alpha, 1.0f, 1e-8f);
    }
    {
      f32 hue, saturation, value, alpha;
      geo_color_to_hsv(geo_color(0.75f, 1.0f, 0.5f, 1.0f), &hue, &saturation, &value, &alpha);
      check_eq_float(hue, 0.25f, 1e-8f);
      check_eq_float(saturation, 0.5f, 1e-8f);
      check_eq_float(value, 1.0f, 1e-8f);
      check_eq_float(alpha, 1.0f, 1e-8f);
    }
  }

  it("round-trips hsv conversion") {
    Rng* rng = rng_create_xorwow(g_allocScratch, 42);
    for (u32 i = 0; i != 100; ++i) {
      const f32      r        = rng_sample_f32(rng);
      const f32      g        = rng_sample_f32(rng);
      const f32      b        = rng_sample_f32(rng);
      const f32      a        = rng_sample_f32(rng);
      const GeoColor colorOrg = geo_color(r, g, b, a);

      f32 h, s, v, a2;
      geo_color_to_hsv(colorOrg, &h, &s, &v, &a2);
      check_eq_float(a2, a, 1e-8f);

      const GeoColor color2 = geo_color_from_hsv(h, s, v, a2);
      check_eq_color(colorOrg, color2);
    }
  }

  it("can be packed into 16 bits") {
    const GeoColor c = geo_color(0.1337f, 13.37f, 0.42f, 4.2f);

    f16 packed[4];
    geo_color_pack_f16(c, packed);

    check_eq_float(float_f16_to_f32(packed[0]), c.r, 1e-2f);
    check_eq_float(float_f16_to_f32(packed[1]), c.g, 1e-2f);
    check_eq_float(float_f16_to_f32(packed[2]), c.b, 1e-2f);
    check_eq_float(float_f16_to_f32(packed[3]), c.a, 1e-2f);
  }

  it("can be unpacked from 16 bits") {
    const GeoColor c1 = geo_color(0.1337f, 13.37f, 0.42f, 4.2f);

    f16 packed[4];
    geo_color_pack_f16(c1, packed);

    const GeoColor c2 = geo_color_unpack_f16(packed);

    check_eq_float(c2.r, c1.r, 1e-2f);
    check_eq_float(c2.g, c1.g, 1e-2f);
    check_eq_float(c2.b, c1.b, 1e-2f);
    check_eq_float(c2.a, c1.a, 1e-2f);
  }
}
