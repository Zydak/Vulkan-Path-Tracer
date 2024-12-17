#version 460

layout (location = 0) out vec4 outColor;

layout (location = 0) in vec2 inTexCoord;

layout (set = 0, binding = 0) uniform sampler2D uTexture;

void main()
{
	outColor = vec4(texture(uTexture, inTexCoord).rgb, 1.0f);
}