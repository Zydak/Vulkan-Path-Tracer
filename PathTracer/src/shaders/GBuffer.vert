#version 460

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inTangent;
layout (location = 3) in vec3 inBitangent;
layout (location = 4) in vec2 inTexCoord;

struct Material
{
	vec4 Albedo;
	vec4 Emissive;
	float Metallic;
	float Roughness;
};

struct DataOut
{
	Material material;
	vec3 Normal;
	vec2 TexCoord;
};

layout (location = 0) out DataOut dataOut;

layout(push_constant) uniform Push
{
	mat4 model;
	Material material;
} push;

layout(set = 0, binding = 0) uniform GlobalUniforms 
{ 
	mat4 ViewProjectionMat;
	mat4 ViewInverse;
	mat4 ProjInverse;
} ubo;

void main()
{
	vec4 worldPos = push.model * vec4(inPosition, 1.0);
	gl_Position = ubo.ViewProjectionMat * worldPos;
	
	dataOut.TexCoord = inTexCoord;
	dataOut.material = push.material;
	dataOut.Normal = inNormal;
}