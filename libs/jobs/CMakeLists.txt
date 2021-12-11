# --------------------------------------------------------------------------------------------------
# Jobs library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: jobs")

add_library(lib_jobs STATIC
  src/affinity_queue.c
  src/dot.c
  src/graph.c
  src/init.c
  src/job.c
  src/executor.c
  src/scheduler.c
  src/work_queue.c)
target_include_directories(lib_jobs PUBLIC include)
target_link_libraries(lib_jobs PUBLIC lib_core)

add_executable(lib_jobs_test
  test/main.c
  test/test_dot.c
  test/test_executor.c
  test/test_graph.c
  test/test_scheduler.c)
target_link_libraries(lib_jobs_test PRIVATE lib_check)
target_link_libraries(lib_jobs_test PRIVATE lib_jobs)
target_link_libraries(lib_jobs_test PRIVATE lib_log)

configure_test(lib_jobs_test jobs)
configure_debuggable(lib_jobs_test)
