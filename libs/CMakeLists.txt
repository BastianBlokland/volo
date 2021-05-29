# --------------------------------------------------------------------------------------------------
# Libaries cmake file.
# --------------------------------------------------------------------------------------------------

add_library(volo_core STATIC
  core/src/alloc_bump.c
  core/src/alloc_heap.c
  core/src/alloc_page_pal.c
  core/src/alloc.c
  core/src/bits.c
  core/src/bitset.c
  core/src/compare.c
  core/src/diag.c
  core/src/dynarray.c
  core/src/dynbitset.c
  core/src/dynstring.c
  core/src/format.c
  core/src/init.c
  core/src/math.c
  core/src/memory.c
  core/src/sort_bubblesort.c
  core/src/sort_quicksort.c
  core/src/string.c
  core/src/time_pal.c
  core/src/time.c)
target_include_directories(volo_core PUBLIC core/include)

add_executable(volo_core_test
  core/test/main.c
  core/test/test_bits.c
  core/test/test_bitset.c
  core/test/test_compare.c
  core/test/test_dynarray.c
  core/test/test_dynbitset.c
  core/test/test_dynstring.c
  core/test/test_float.c
  core/test/test_format.c
  core/test/test_math.c
  core/test/test_sort.c
  core/test/test_string.c
  core/test/test_time.c)
target_link_libraries(volo_core_test PRIVATE volo_core)
