#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec2 inTexCoord;

layout(location = 0) out vec3 outColor;
layout(location = 1) out int outLightsCount;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec3 outNormal;

layout(set = 0, binding = 0) uniform CameraUbo 
{ 
	mat4 ProjViewMat;
	mat4 ViewInverse;
	mat4 ProjInverse;
} ubo;

layout(push_constant) uniform Push
{
	vec4 color; // .a is lights count
	mat4 model;
} push;

void main()
{
	outWorldPos = vec3(push.model * vec4(inPosition, 1.0f));
    outColor = push.color.xyz;
    outLightsCount = int(push.color.a);
	outNormal = vec3(push.model * vec4(inNormal, 1.0f));

	gl_Position = ubo.ProjViewMat * vec4(outWorldPos, 1.0f);
}