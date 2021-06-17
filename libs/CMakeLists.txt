# --------------------------------------------------------------------------------------------------
# Libaries cmake file.
# --------------------------------------------------------------------------------------------------

add_library(volo_core STATIC
  core/src/alloc_bump.c
  core/src/alloc_heap.c
  core/src/alloc_page_pal.c
  core/src/alloc_scratch.c
  core/src/alloc.c
  core/src/ascii.c
  core/src/bits.c
  core/src/bitset.c
  core/src/compare.c
  core/src/diag.c
  core/src/dynarray.c
  core/src/dynbitset.c
  core/src/dynstring.c
  core/src/file_pal.c
  core/src/file.c
  core/src/format.c
  core/src/init.c
  core/src/math.c
  core/src/memory.c
  core/src/path_pal.c
  core/src/path.c
  core/src/signal_pal.c
  core/src/signal.c
  core/src/sort_bubblesort.c
  core/src/sort_quicksort.c
  core/src/string.c
  core/src/thread_pal.c
  core/src/thread.c
  core/src/time_pal.c
  core/src/time.c
  core/src/tty_pal.c
  core/src/tty.c
  core/src/utf8.c
  core/src/winutils.c)
target_include_directories(volo_core PUBLIC core/include)

add_executable(volo_core_test
  core/test/main.c
  core/test/test_alloc_bump.c
  core/test/test_alloc_page.c
  core/test/test_alloc_scratch.c
  core/test/test_ascii.c
  core/test/test_bits.c
  core/test/test_bitset.c
  core/test/test_compare.c
  core/test/test_dynarray.c
  core/test/test_dynbitset.c
  core/test/test_dynstring.c
  core/test/test_file.c
  core/test/test_float.c
  core/test/test_format.c
  core/test/test_macro.c
  core/test/test_math.c
  core/test/test_memory.c
  core/test/test_path.c
  core/test/test_sort.c
  core/test/test_string.c
  core/test/test_thread.c
  core/test/test_time.c
  core/test/test_utf8.c
  core/test/test_winutils.c)
target_link_libraries(volo_core_test PRIVATE volo_core)
