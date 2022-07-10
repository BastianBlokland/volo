#include "check_spec.h"
#include "core_array.h"
#include "geo_sphere.h"

#include "utils_internal.h"

spec(sphere) {

  it("can test the intersection with a ray") {
    static const struct {
      GeoRay    ray;
      GeoSphere sphere;
      bool      hit;
    } g_data[] = {
        {.ray = {-2, 1, 0, .dir = {2, 0, 0}}, .sphere = {2, 0, 0, .radius = 2}, .hit = true},
        {.ray = {-2, 0, 0, .dir = {2, 0, 0}}, .sphere = {2, 2, 0, .radius = 2}, .hit = true},
        {.ray = {-2, 0, 0, .dir = {2, 0, 0}}, .sphere = {0, 0, 0, .radius = 2}, .hit = true},
        {.ray = {-2, 2, 0, .dir = {2, -1, 2}}, .sphere = {0, 0, 0, .radius = 2}, .hit = true},
        {.ray = {2, 1, 0, .dir = {2, 0, 0}}, .sphere = {2, 0, 0, .radius = 2}, .hit = true},
        {.ray = {-2, 1, 0, .dir = {-1, 0, 0}}, .sphere = {2, 0, 0, .radius = 2}, .hit = false},
        {.ray = {-5, 1, 0, .dir = {2, 0.4f, 0}}, .sphere = {2, 0, 0, .radius = 2}, .hit = false},
    };

    for (u32 i = 0; i != array_elems(g_data); ++i) {
      const GeoRay ray = {.point = g_data[i].ray.point, geo_vector_norm(g_data[i].ray.dir)};

      const f32  hitT   = geo_sphere_intersect_ray(&g_data[i].sphere, &ray);
      const bool wasHit = hitT >= 0.0f;

      check_msg(
          wasHit == g_data[i].hit,
          "[data {}] Expected: hit == {}, got: hit == {}",
          fmt_int(i),
          fmt_bool(g_data[i].hit),
          fmt_bool(wasHit));
    }
  }

  it("can compute the intersection time with a ray") {
    {
      const GeoSphere sphere = {.point = {0, 0, 0}, .radius = 1.0f};
      const GeoRay    ray    = {.point = {0, 0, -2}, .dir = geo_forward};
      const f32       hitT   = geo_sphere_intersect_ray(&sphere, &ray);
      check_eq_float(hitT, 1, 1e-6);
    }
    {
      const GeoSphere sphere = {.point = {0, 0, 0}, .radius = 1.0f};
      const GeoRay    ray    = {.point = {0, 0, 2}, .dir = geo_backward};
      const f32       hitT   = geo_sphere_intersect_ray(&sphere, &ray);
      check_eq_float(hitT, 1, 1e-6);
    }
    {
      const GeoSphere sphere = {.point = {0, 0, 0}, .radius = 1.0f};
      const GeoRay    ray    = {.point = {0, 0, 0.5f}, .dir = geo_forward};
      const f32       hitT   = geo_sphere_intersect_ray(&sphere, &ray);
      check_eq_float(hitT, 0.5f, 1e-6);
    }
  }
}
