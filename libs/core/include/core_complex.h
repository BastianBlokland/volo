#pragma once
#include "core.h"

typedef struct sComplex {
  f64 real, imaginary;
} Complex;

/**
 * Construct a Complex number from its Real and Imaginary parts.
 */
#define complex(_REAL_, _IMAGINARY_) ((Complex){(_REAL_), (_IMAGINARY_)})

/**
 * Add two complex numbers.
 */
Complex complex_add(Complex, Complex);

/**
 * Subtract two complex numbers.
 */
Complex complex_sub(Complex, Complex);

/**
 * Multiply two complex numbers.
 */
Complex complex_mul(Complex, Complex);

/**
 * Compute the natural logarithm e raised to the power of exp.
 */
Complex complex_exp(Complex exp);

/**
 * Create a formatting argument for a complex number.
 * NOTE: _COMPLEX_ is expanded multiple times, so care must be taken when providing expressions.
 */
#define complex_fmt(_COMPLEX_)                                                                     \
  fmt_list_lit(fmt_float((_COMPLEX_).real), fmt_float((_COMPLEX_).imaginary))
