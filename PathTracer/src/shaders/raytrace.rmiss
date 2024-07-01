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
	rayDir = Rotate(rayDir, vec3(1, 0, 0), -pcRay.EnvAltitude);
	rayDir = Rotate(rayDir, vec3(0, 1, 0), -pcRay.EnvAzimuth);
	vec2 uv = directionToSphericalEnvmap(rayDir);
	vec3 color = texture(uEnvMap, uv).xyz;

    payload.HitValue = color;
	payload.Depth = DEPTH_INFINITE;
}