#include "check.h"
#include "core.h"
#include "jobs.h"
#include "log.h"

int main(const int argc, const char** argv) {
  core_init();
  jobs_init();
  log_init();

  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  CheckDef* check = check_create(g_alloc_heap);
  register_spec(check, manager);
  register_spec(check, loader_font_ttf);
  register_spec(check, loader_graphic);
  register_spec(check, loader_mesh_obj);
  register_spec(check, loader_raw);
  register_spec(check, loader_shader_spv);
  register_spec(check, loader_texture_ppm);
  register_spec(check, loader_texture_tga);

  const int exitCode = check_app(check, argc, argv);

  check_destroy(check);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
