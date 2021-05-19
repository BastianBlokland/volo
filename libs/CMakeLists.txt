# --------------------------------------------------------------------------------------------------
# Libaries cmake file.
# --------------------------------------------------------------------------------------------------

add_library(volo_core STATIC
  core/src/alloc_heap.c
  core/src/alloc.c
  core/src/bits.c
  core/src/diag.c
  core/src/dynarray.c
  core/src/memory.c
  core/src/string.c)
target_include_directories(volo_core PUBLIC core/include)

add_executable(volo_core_test
  core/test/bits_test.c
  core/test/dynarray_test.c
  core/test/main.c
  core/test/string_test.c)
target_link_libraries(volo_core_test PRIVATE volo_core)
