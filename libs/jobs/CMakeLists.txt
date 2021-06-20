# --------------------------------------------------------------------------------------------------
# Jobs library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "Configuring jobs library")

add_library(volo_jobs STATIC
  src/init.c
  src/jobdef.c)
target_include_directories(volo_jobs PUBLIC include)
target_link_libraries(volo_jobs PUBLIC volo_core)

add_executable(volo_jobs_test
  test/main.c
  test/test_jobdef.c)
target_link_libraries(volo_jobs_test PRIVATE volo_jobs)
