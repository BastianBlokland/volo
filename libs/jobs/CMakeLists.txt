# --------------------------------------------------------------------------------------------------
# Jobs library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(jobs STATIC
  src/affinity_queue.c
  src/dot.c
  src/executor.c
  src/graph.c
  src/init.c
  src/job.c
  src/scheduler.c
  src/work_queue.c
  )
target_include_directories(jobs PUBLIC include)
target_link_libraries(jobs PUBLIC core)
target_link_libraries(jobs PRIVATE trace)

add_executable(jobs_test
  test/config.c
  test/test_dot.c
  test/test_executor.c
  test/test_graph.c
  test/test_scheduler.c
  )
target_link_libraries(jobs_test PRIVATE app_check jobs)
