# --------------------------------------------------------------------------------------------------
# Jobs library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: jobs")

add_library(lib_jobs STATIC
  src/affinity_queue.c
  src/dot.c
  src/executor.c
  src/graph.c
  src/init.c
  src/job.c
  src/scheduler.c
  src/work_queue.c
  )
target_include_directories(lib_jobs PUBLIC include)
target_link_libraries(lib_jobs PUBLIC lib_core)

add_executable(test_lib_jobs
  test/config.c
  test/test_dot.c
  test/test_executor.c
  test/test_graph.c
  test/test_scheduler.c
  )
target_link_libraries(test_lib_jobs PRIVATE lib_app_check lib_jobs)

configure_test(test_lib_jobs SHORT_NAME jobs)
configure_debuggable(test_lib_jobs)
