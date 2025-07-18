# --------------------------------------------------------------------------------------------------
# Core library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: core")

add_library(lib_core STATIC
  src/alloc_block.c
  src/alloc_bump.c
  src/alloc_chunked.c
  src/alloc_heap.c
  src/alloc_page_pal.c
  src/alloc_pagecache.c
  src/alloc_persist.c
  src/alloc_scratch.c
  src/alloc_stdlib.c
  src/alloc_tracker.c
  src/alloc.c
  src/ascii.c
  src/base64.c
  src/bc.c
  src/bits.c
  src/bitset.c
  src/compare.c
  src/complex.c
  src/deflate.c
  src/diag_pal.c
  src/diag.c
  src/dynarray.c
  src/dynbitset.c
  src/dynlib_pal.c
  src/dynlib.c
  src/dynstring.c
  src/env_pal.c
  src/env.c
  src/file_iterator_pal.c
  src/file_iterator.c
  src/file_monitor_pal.c
  src/file_monitor.c
  src/file_pal.c
  src/file.c
  src/float.c
  src/format.c
  src/gzip.c
  src/init.c
  src/math.c
  src/memory.c
  src/noise.c
  src/path_pal.c
  src/path.c
  src/process_pal.c
  src/process.c
  src/rng.c
  src/search.c
  src/shuffle.c
  src/signal_pal.c
  src/signal.c
  src/sort.c
  src/string.c
  src/stringtable.c
  src/symbol_pal.c
  src/symbol.c
  src/thread_pal.c
  src/thread.c
  src/time_pal.c
  src/time.c
  src/tty_pal.c
  src/tty.c
  src/unicode.c
  src/utf8.c
  src/winutils.c
  src/zlib.c
  )
target_include_directories(lib_core PUBLIC include)
if(${VOLO_PLATFORM} STREQUAL "linux")
  target_link_libraries(lib_core PRIVATE dl) # DynamicLinking library.
  target_link_libraries(lib_core PRIVATE m) # Math library.
endif()

add_executable(test_lib_core
  test/config.c
  test/test_alloc_block.c
  test/test_alloc_bump.c
  test/test_alloc_chunked.c
  test/test_alloc_page.c
  test/test_alloc_scratch.c
  test/test_array.c
  test/test_ascii.c
  test/test_base64.c
  test/test_bc.c
  test/test_bits.c
  test/test_bitset.c
  test/test_compare.c
  test/test_complex.c
  test/test_deflate.c
  test/test_dynarray.c
  test/test_dynbitset.c
  test/test_dynlib.c
  test/test_dynstring.c
  test/test_env.c
  test/test_file_iterator.c
  test/test_file_monitor.c
  test/test_file.c
  test/test_float.c
  test/test_format.c
  test/test_gzip.c
  test/test_macro.c
  test/test_math.c
  test/test_memory.c
  test/test_path.c
  test/test_process.c
  test/test_rng.c
  test/test_search.c
  test/test_shuffle.c
  test/test_simd.c
  test/test_sort.c
  test/test_string.c
  test/test_stringtable.c
  test/test_symbol.c
  test/test_thread.c
  test/test_time.c
  test/test_types.c
  test/test_unicode.c
  test/test_utf8.c
  test/test_winutils.c
  test/test_zlib.c
  )
target_link_libraries(test_lib_core PRIVATE lib_app_check lib_core)

configure_test(test_lib_core SHORT_NAME core)
configure_debuggable(test_lib_core)

# Helper executable that is used as part of the process tests.
add_executable(test_lib_core_helper test/exec_helper.c)
target_link_libraries(test_lib_core_helper PRIVATE lib_app_cli)
add_dependencies(test_lib_core test_lib_core_helper)
