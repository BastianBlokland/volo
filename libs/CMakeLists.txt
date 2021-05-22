# --------------------------------------------------------------------------------------------------
# Libaries cmake file.
# --------------------------------------------------------------------------------------------------

add_library(volo_core STATIC
  core/src/alloc_heap.c
  core/src/alloc.c
  core/src/bits.c
  core/src/bitset.c
  core/src/diag.c
  core/src/dynarray.c
  core/src/dynbitset.c
  core/src/dynstring.c
  core/src/memory.c
  core/src/string.c)
target_include_directories(volo_core PUBLIC core/include)

add_executable(volo_core_test
  core/test/main.c
  core/test/test_bits.c
  core/test/test_bitset.c
  core/test/test_dynarray.c
  core/test/test_dynbitset.c
  core/test/test_dynstring.c
  core/test/test_string.c)
target_link_libraries(volo_core_test PRIVATE volo_core)
