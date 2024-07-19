#version 460

layout(location = 0) in vec3 inColor;
layout(location = 1) in flat int inLightsCount;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec3 inNormal;

layout (location = 0) out vec4 outColor;

struct LightBufferEntry
{
	vec4 Color;
	vec3 Position;
};

layout(set = 1, binding = 0) buffer LightsUBO 
{ 
	LightBufferEntry lights[];
} lightsUbo;

const float M_PI = 3.1415926535897F;  // PI

void main()
{
	vec3 color = inColor / M_PI;

	vec3 lightPos = lightsUbo.lights[0].Position;
	vec3 lightColor = lightsUbo.lights[0].Color.xyz * lightsUbo.lights[0].Color.a;
	vec3 L = normalize(lightPos - inWorldPos);
	float dist = length(lightPos - inWorldPos);
	color *= lightColor * dot(L, inNormal) / dist;

	outColor = vec4(color, 1.0f);
}