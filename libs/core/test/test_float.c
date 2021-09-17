#include "check_spec.h"
#include "core_float.h"

spec(float) {

  it("can detect a NaN float") {
    check(float_isnan(f32_nan));
    check(float_isnan(f64_nan));
  }

  it("can detect an infinite float") {
    check(float_isinf(f32_inf));
    check(float_isinf(f64_inf));
  }
}
