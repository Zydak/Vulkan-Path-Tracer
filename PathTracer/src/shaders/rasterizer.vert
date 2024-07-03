#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec2 inTexCoord;

layout(location = 0) out vec3 outColor;

layout(set = 0, binding = 0) uniform CameraUbo 
{ 
	mat4 ProjViewMat;
	mat4 ViewInverse;
	mat4 ProjInverse;
} ubo;

struct Material
{
    vec4 Color;
    vec4 EmissiveColor;
    float Metallic;
    float Roughness;
    float SubsurfaceScattering;
    float Anisotropy;
    float SpecularTint;
    float SpecularStrength;

    float Ior;
    float SpecTrans;
    float ClearCoat;
    float ClearCoatRoughness;

    float eta;
    float ax;
    float ay;
};

layout(push_constant) uniform Push
{
	mat4 model;
	Material material;
} push;

void main()
{
	gl_Position = ubo.ProjViewMat * push.model * vec4(inPosition, 1.0f);
    outColor = push.material.Color.xyz;
}