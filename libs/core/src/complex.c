#include "core_complex.h"

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
