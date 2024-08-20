#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(push_constant) uniform PushConstants {
	mat4 pModelViewProj;
};

layout(location = 0) out vec2 outTexCoord;

void main()
{
	gl_Position = pModelViewProj * vec4(inPosition, 1.0f);
	outTexCoord = inTexCoord;
}