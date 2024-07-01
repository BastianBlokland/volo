#include "binding.glsl"
#include "global.glsl"
#include "light.glsl"
#include "math.glsl"
#include "pbr.glsl"
#include "rand.glsl"
#include "texture.glsl"

bind_spec(0) const f32 s_coverageScale     = 100;
bind_spec(1) const f32 s_coveragePanSpeedX = 1.5;
bind_spec(2) const f32 s_coveragePanSpeedY = 0.25;

/**
 * Poisson Disk Sampling, random 2d points that maintain a minimal distance to each-other.
 * https://en.wikipedia.org/wiki/Supersampling#Poisson_disk
 */
const u32   c_poissonDiskSampleCount                = 16;
const f32   c_pointDiskSampleCountInv               = 1.0 / c_poissonDiskSampleCount;
const f32v2 c_poissonDisk[c_poissonDiskSampleCount] = {
    f32v2(-0.94201624, -0.39906216),
    f32v2(0.94558609, -0.76890725),
    f32v2(-0.094184101, -0.92938870),
    f32v2(0.34495938, 0.29387760),
    f32v2(-0.91588581, 0.45771432),
    f32v2(-0.81544232, -0.87912464),
    f32v2(-0.38277543, 0.27676845),
    f32v2(0.97484398, 0.75648379),
    f32v2(0.44323325, -0.97511554),
    f32v2(0.53742981, -0.47373420),
    f32v2(-0.26496911, -0.41893023),
    f32v2(0.79197514, 0.19090188),
    f32v2(-0.24188840, 0.99706507),
    f32v2(-0.81409955, 0.91437590),
    f32v2(0.19984126, 0.78641367),
    f32v2(0.14383161, -0.14100790),
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global_img(0) uniform sampler2D u_texGeoData0;
bind_global_img(1) uniform sampler2D u_texGeoData1;
bind_global_img(2) uniform sampler2D u_texGeoDepth;
bind_global_img(4) uniform sampler2DShadow u_texShadow;

bind_graphic_img(0) uniform sampler2D u_texCoverageMask;

bind_internal(0) in f32v2 in_texcoord;
bind_internal(1) in flat f32v3 in_direction;
bind_internal(2) in flat f32v4 in_radianceFlags; // x, y, z: radiance, w: flags.
bind_internal(3) in flat f32v4 in_shadowParams;  // x: filterSize, y, z, w: unused.
bind_internal(4) in flat f32m4 in_shadowViewProj;

bind_internal(0) out f32v3 out_color;

f32 coverage_frac(const f32v3 worldPos) {
  const f32v2 texOffset = f32v2(s_coveragePanSpeedX, s_coveragePanSpeedY) * u_global.time.x;
  const f32v2 texCoord  = (worldPos.xz + texOffset) / s_coverageScale;
  return texture(u_texCoverageMask, texCoord).r;
}

f32v3 shadow_map_coord(const f32v3 worldPos) {
  const f32v4 clipPos = in_shadowViewProj * f32v4(worldPos, 1.0);
  return f32v3(clipPos.xy * 0.5 + 0.5, clipPos.z);
}

f32 shadow_frac(const f32v3 worldPos) {
  const f32v3 shadCoord = shadow_map_coord(worldPos);
  if (shadCoord.z <= 0.0) {
    return 0.0;
  }

  /**
   * Compute the filter size (in shadow space) from the filterSize param (in world space), by taking
   * the distance between two world-space points.
   */
  const f32v3 shadRefCoord = shadow_map_coord(worldPos + f32v3(0, 0, in_shadowParams.x));
  const f32   filterSize   = length(shadRefCoord.xz - shadCoord.xz);

  /**
   * Randomize the rotation of the sample points based on the position, this greatly reduces the
   * visible patterns at the tradeoff of some noise.
   */
  const f32   randVal = rand_f32(f32v4(worldPos, 0));
  const f32m2 rotMat  = math_rotate_mat_f32m2(randVal * c_pi * 2);

  f32 shadowSum = 0;
  for (u32 i = 0; i < c_poissonDiskSampleCount; ++i) {
    const f32v2 poissonCoord = rotMat * c_poissonDisk[i];
    const f32v3 sampleCoord  = f32v3(shadCoord.xy + poissonCoord * filterSize, shadCoord.z);
    shadowSum += texture(u_texShadow, sampleCoord);
  }
  return shadowSum * c_pointDiskSampleCountInv;
}

void main() {
  GeometryEncoded geoEncoded;
  geoEncoded.data0 = texture(u_texGeoData0, in_texcoord);
  geoEncoded.data1 = texture(u_texGeoData1, in_texcoord);

  const Geometry geo = geometry_decode(geoEncoded);

  const f32   depth    = texture(u_texGeoDepth, in_texcoord).r;
  const f32v3 clipPos  = f32v3(in_texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos = clip_to_world_pos(u_global, clipPos);
  const f32v3 viewDir  = normalize(u_global.camPosition.xyz - worldPos);

  const u32 lightFlags = floatBitsToUint(in_radianceFlags.w);

  f32v3 effectiveRadiance = in_radianceFlags.xyz;

  if ((lightFlags & c_lightFlagsCoverageMask) != 0) {
    effectiveRadiance *= coverage_frac(worldPos);
  }

  if ((lightFlags & c_lightFlagsShadows) != 0) {
    effectiveRadiance *= 1.0 - shadow_frac(worldPos);
  }

  PbrSurface surf;
  surf.position  = worldPos;
  surf.color     = geo.color;
  surf.normal    = geo.normal;
  surf.roughness = geo.roughness;

  out_color = pbr_light_dir(effectiveRadiance, in_direction, viewDir, surf);
}
