# --------------------------------------------------------------------------------------------------
# Applications cmake file.
# --------------------------------------------------------------------------------------------------

add_executable(volo volo.c)
target_link_libraries(volo PRIVATE volo_core)
configure_debuggable(volo)
