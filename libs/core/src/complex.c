#include "core_complex.h"

Complex complex_mul(const Complex a, const Complex b) {
  return (Complex){
      .real      = a.real * b.real - a.imaginary * b.imaginary,
      .imaginary = a.real * b.imaginary + a.imaginary * b.real,
  };
}
