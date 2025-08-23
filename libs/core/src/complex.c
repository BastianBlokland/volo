#include "core/complex.h"
#include "core/intrinsic.h"
#include "core/math.h"

Complex complex_add(const Complex a, const Complex b) {
  return (Complex){
      .real      = a.real + b.real,
      .imaginary = a.imaginary + b.imaginary,
  };
}

Complex complex_sub(const Complex a, const Complex b) {
  return (Complex){
      .real      = a.real - b.real,
      .imaginary = a.imaginary - b.imaginary,
  };
}

Complex complex_mul(const Complex a, const Complex b) {
  return (Complex){
      .real      = a.real * b.real - a.imaginary * b.imaginary,
      .imaginary = a.real * b.imaginary + a.imaginary * b.real,
  };
}

Complex complex_exp(const Complex exp) {
  return (Complex){
      .real      = intrinsic_pow_f64(math_e_f64, exp.real) * intrinsic_cos_f64(exp.imaginary),
      .imaginary = intrinsic_pow_f64(math_e_f64, exp.real) * intrinsic_sin_f64(exp.imaginary),
  };
}
