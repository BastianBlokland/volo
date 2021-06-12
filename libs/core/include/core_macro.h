#pragma once

/**
 * Count the number of arguments in a variadic macro. Supports up to 10 arguments.
 * Implemented by 'sliding' an argument list based on how many VA_ARGS are passed.
 * Relies on the '##__VA_ARGS__' extension for empty argument lists.
 * More information: https://codecraft.co/2014/11/25/variadic-macros-tricks/
 */
#define IMPL_GET_10TH_ARG(_1_, _2_, _3_, _4_, _5_, _6_, _7_, _8_, _9_, _10_, _11_, _N_, ...) _N_
#define COUNT_VA_ARGS(...)                                                                         \
  IMPL_GET_10TH_ARG("ignored", ##__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

/**
 * Return all but the first argument.
 */
#define VA_ARGS_SKIP_FIRST(_FIRST_, ...) __VA_ARGS__
