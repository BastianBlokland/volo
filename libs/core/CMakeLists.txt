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
  src/alloc_persist.c
  src/alloc_scratch.c
  src/alloc.c
  src/ascii.c
  src/base64.c
  src/bits.c
  src/bitset.c
  src/compare.c
  src/diag_pal.c
  src/diag.c
  src/dynarray.c
  src/dynbitset.c
  src/dynstring.c
  src/env_pal.c
  src/file_monitor_pal.c
  src/file_monitor.c
  src/file_pal.c
  src/file.c
  src/float.c
  src/format.c
  src/init.c
  src/math.c
  src/memory.c
  src/noise_lut.c
  src/noise.c
  src/path_pal.c
  src/path.c
  src/rng.c
  src/search.c
  src/shuffle.c
  src/signal_pal.c
  src/signal.c
  src/sort.c
  src/string.c
  src/stringtable.c
  src/thread_pal.c
  src/thread.c
  src/time_pal.c
  src/time.c
  src/tty_pal.c
  src/tty.c
  src/unicode.c
  src/utf8.c
  src/winutils.c
  )
target_include_directories(lib_core PUBLIC include)
if(${VOLO_PLATFORM} STREQUAL "linux")
  # Link against the math library on linux.
  target_link_libraries(lib_core PRIVATE m)
elseif(${VOLO_PLATFORM} STREQUAL "win32")
  # Link against 'winmm.lib' (Windows Multimedia API) for controlling the scheduling interval.
  target_link_libraries(lib_core PRIVATE winmm.lib)
endif()

add_executable(lib_core_test
  test/app.c
  test/test_alloc_block.c
  test/test_alloc_bump.c
  test/test_alloc_chunked.c
  test/test_alloc_page.c
  test/test_alloc_scratch.c
  test/test_array.c
  test/test_ascii.c
  test/test_base64.c
  test/test_bits.c
  test/test_bitset.c
  test/test_compare.c
  test/test_dynarray.c
  test/test_dynbitset.c
  test/test_dynstring.c
  test/test_env.c
  test/test_file_monitor.c
  test/test_file.c
  test/test_float.c
  test/test_format.c
  test/test_macro.c
  test/test_math.c
  test/test_memory.c
  test/test_path.c
  test/test_rng.c
  test/test_search.c
  test/test_shuffle.c
  test/test_sort.c
  test/test_string.c
  test/test_stringtable.c
  test/test_thread.c
  test/test_time.c
  test/test_unicode.c
  test/test_utf8.c
  test/test_winutils.c
  )
target_link_libraries(lib_core_test PRIVATE lib_app_check)
target_link_libraries(lib_core_test PRIVATE lib_core)

configure_test(lib_core_test SHORT_NAME core)
configure_debuggable(lib_core_test)
