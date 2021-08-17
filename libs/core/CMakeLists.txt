# --------------------------------------------------------------------------------------------------
# Core library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "Configuring core library")

add_library(volo_core STATIC
  src/alloc_bump.c
  src/alloc_heap.c
  src/alloc_page_pal.c
  src/alloc_scratch.c
  src/alloc.c
  src/ascii.c
  src/base64.c
  src/bits.c
  src/bitset.c
  src/compare.c
  src/diag.c
  src/dynarray.c
  src/dynbitset.c
  src/dynstring.c
  src/file_pal.c
  src/file.c
  src/format.c
  src/init.c
  src/math.c
  src/memory.c
  src/path_pal.c
  src/path.c
  src/rng.c
  src/shuffle.c
  src/signal_pal.c
  src/signal.c
  src/sort_bubblesort.c
  src/sort_quicksort.c
  src/string.c
  src/thread_pal.c
  src/thread.c
  src/time_pal.c
  src/time.c
  src/tty_pal.c
  src/tty.c
  src/utf8.c
  src/winutils.c)
target_include_directories(volo_core PUBLIC include)
if(${VOLO_PLATFORM} STREQUAL "linux")
  # Link against the math library on linux.
  target_link_libraries(volo_core PRIVATE m)
endif()

add_executable(volo_core_test
  test/main.c
  test/test_alloc_bump.c
  test/test_alloc_page.c
  test/test_alloc_scratch.c
  test/test_ascii.c
  test/test_base64.c
  test/test_bits.c
  test/test_bitset.c
  test/test_compare.c
  test/test_dynarray.c
  test/test_dynbitset.c
  test/test_dynstring.c
  test/test_file.c
  test/test_float.c
  test/test_format.c
  test/test_macro.c
  test/test_math.c
  test/test_memory.c
  test/test_path.c
  test/test_rng.c
  test/test_shuffle.c
  test/test_sort.c
  test/test_string.c
  test/test_thread.c
  test/test_time.c
  test/test_utf8.c
  test/test_winutils.c)
target_link_libraries(volo_core_test PRIVATE volo_check)
target_link_libraries(volo_core_test PRIVATE volo_cli)
target_link_libraries(volo_core_test PRIVATE volo_core)
