# --------------------------------------------------------------------------------------------------
# Libaries cmake file.
# --------------------------------------------------------------------------------------------------

add_library(volo_core STATIC
  core/src/bits.c
  core/src/diag.c
  core/src/dynarray.c
  core/src/memory.c)
target_include_directories(volo_core PUBLIC core/include)

add_executable(volo_core_test
  core/test/bits_test.c
  core/test/dynarray_test.c
  core/test/main.c)
target_link_libraries(volo_core_test PRIVATE volo_core)
