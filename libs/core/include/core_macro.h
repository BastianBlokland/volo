#pragma once

/**
 * Count the number of arguments in a variadic macro. Supports up to 10 arguments.
 * Implemented by 'sliding' an argument list based on how many VA_ARGS are passed.
 * Relies on the '##__VA_ARGS__' extension for empty argument lists.
 * More information: https://codecraft.co/2014/11/25/variadic-macros-tricks/
 */
#define IMPL_GET_NTH_ARG(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, N, ...) N
#define COUNT_VA_ARGS(...)                                                                         \
  IMPL_GET_NTH_ARG("ignored", ##__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
