#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "raycommon.glsl"

layout(location = 0) rayPayloadInEXT hitPayload payload;

layout (set = 1, binding = 1) uniform sampler2D uEnvMap;

layout(push_constant) uniform _PushConstantRay
{
	PushConstantRay pcRay;
};

void main()
{
	vec3 rayDir = payload.RayDirection;
	vec3 color;
#ifdef SHOW_SKYBOX
	rayDir = Rotate(rayDir, vec3(1, 0, 0), -pcRay.EnvAltitude + M_PI);
	rayDir = Rotate(rayDir, vec3(0, 1, 0), -pcRay.EnvAzimuth);
	vec2 uv = directionToSphericalEnvmap(rayDir);

	if (payload.LastEvent == LAST_EVENT_SURFACE || payload.Depth == 0)
	{
		color = texture(uEnvMap, uv).xyz;
	}
	else
	{
		color = vec3(0.0f); // The color in volumes is already accounted for with importance sampling
	}
#else
	if (payload.Depth == 0 || payload.LastEvent == LAST_EVENT_VOLUME)
	{
		color = vec3(0.0f);
	}
	else
	{
		color = texture(uEnvMap, uv).xyz;
	}
#endif

	if (payload.Depth == 0)
		payload.MissedAllGeometry = true;

    payload.HitValue += color;
	payload.Depth = DEPTH_INFINITE;
	payload.SurfaceAlbedo = color;
	payload.SurfaceNormal = vec3(0.0f);
}