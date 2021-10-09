# --------------------------------------------------------------------------------------------------
# Jobs library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: jobs")

add_library(volo_jobs STATIC
  src/dot.c
  src/graph.c
  src/init.c
  src/job.c
  src/executor.c
  src/scheduler.c
  src/work_queue.c)
target_include_directories(volo_jobs PUBLIC include)
target_link_libraries(volo_jobs PUBLIC volo_core)

add_executable(volo_jobs_test
  test/main.c
  test/test_dot.c
  test/test_executor.c
  test/test_graph.c
  test/test_scheduler.c)
target_link_libraries(volo_jobs_test PRIVATE volo_check)
target_link_libraries(volo_jobs_test PRIVATE volo_jobs)
target_link_libraries(volo_jobs_test PRIVATE volo_log)

configure_test(volo_jobs_test jobs)
configure_debuggable(volo_jobs_test)
