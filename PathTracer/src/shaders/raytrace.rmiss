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

vec3 SampleHenyeyGreenstein(vec3 incomingDir, float g, float rand1, float rand2) 
{
    float phi = 2.0 * M_PI * rand1;
    float cosTheta;
    if (abs(g) < 1e-3) 
    {
        cosTheta = 1.0 - 2.0 * rand2;
    } 
    else 
    {
        float sqrTerm = (1.0 - g * g) / (1.0 - g + 2.0 * g * rand2);
        cosTheta = (1.0 + g * g - sqrTerm * sqrTerm) / (2.0 * g);
    }
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    vec3 tangent = normalize(cross(incomingDir, abs(incomingDir.x) > 0.1 ? vec3(0, 1, 0) : vec3(1, 0, 0)));
    vec3 bitangent = cross(incomingDir, tangent);
    vec3 scatteredDir = incomingDir * cosTheta + tangent * sinTheta * cos(phi) + bitangent * sinTheta * sin(phi);
    return normalize(scatteredDir);
}

float SampleExponentialDistance(float extinctionCoefficient, float rand) 
{
    return -log(1.0 - rand) / extinctionCoefficient;
}

float CalculateOpticalDepth(float extinctionCoefficient, float distance) 
{
    return extinctionCoefficient * distance;
}

bool ShouldScatter(float opticalDepth, float rand) 
{
    return rand <= 1.0 - exp(-opticalDepth);
}

void main()
{
    vec3 rayDir = payload.RayDirection;
	rayDir = Rotate(rayDir, vec3(1, 0, 0), -pcRay.EnvAltitude);
	rayDir = Rotate(rayDir, vec3(0, 1, 0), -pcRay.EnvAzimuth);
	vec2 uv = directionToSphericalEnvmap(rayDir);
	vec4 color = texture(uEnvMap, uv);
	if (payload.Depth != 0)
    {
#ifdef SAMPLE_ENV_MAP
        //const float misWeight = color.w / (color.w + payload.Pdf);
        //const vec3 w = (color.xyz / color.w) * misWeight;
        //
        //vec3 contribution = w * payload.Bsdf;
        //
	    //payload.HitValue += contribution;
        payload.HitValue = color.rgb;
#else
        payload.HitValue = color.rgb;
#endif
    }
    else
	{
#ifdef SHOW_SKYBOX
	    payload.HitValue = color.rgb;
#endif
		payload.MissedAllGeometry = true; // set to true to eliminate collecting more samples of the pixel
	}

	payload.Depth = DEPTH_INFINITE;
}