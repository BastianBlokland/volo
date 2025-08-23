#pragma once

// clang-format off

/**
 * Count the number of arguments in a variadic macro. Supports up to 40 arguments.
 * Implemented by 'sliding' an argument list based on how many VA_ARGS are passed.
 * Relies on the '##__VA_ARGS__' extension for empty argument lists.
 * More information: https://codecraft.co/2014/11/25/variadic-macros-tricks/
 */
#define IMPL_GET_40TH_ARG(                                                                         \
  _01_, _02_, _03_, _04_, _05_, _06_, _07_, _08_, _09_, _10_,                                      \
  _11_, _12_, _13_, _14_, _15_, _16_, _17_, _18_, _19_, _20_,                                      \
  _21_, _22_, _23_, _24_, _25_, _26_, _27_, _28_, _29_, _30_,                                      \
  _31_, _32_, _33_, _34_, _35_, _36_, _37_, _38_, _39_, _40_,                                      \
  _N_, ...) _N_

#define COUNT_VA_ARGS(...) IMPL_GET_40TH_ARG("ignored", ##__VA_ARGS__,                             \
  39, 38, 37, 36, 35, 34, 33, 3, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

// clang-format on

/**
 * Return all but the first argument.
 */
#define VA_ARGS_SKIP_FIRST(_FIRST_, ...) __VA_ARGS__
