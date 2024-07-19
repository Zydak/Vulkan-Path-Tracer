#version 460

layout (location = 0) out vec4 outAlbedo;
layout (location = 1) out vec4 outNormal;
layout (location = 2) out vec2 outRoughnessMetallness;
layout (location = 3) out vec4 outEmissive;

layout(set = 1, binding = 0) uniform sampler2D uAlbedoTexture;
layout(set = 1, binding = 1) uniform sampler2D uNormalTexture;
layout(set = 1, binding = 2) uniform sampler2D uRoghnessTexture;
layout(set = 1, binding = 3) uniform sampler2D uMetallnessTexture;

struct Material
{
	vec4 Albedo;
	vec4 Emissive;
	float Metallic;
	float Roughness;
};

struct DataIn
{
	Material material;
	vec3 Normal;
	vec2 TexCoord;
};

layout (location = 0) in DataIn dataIn;

void main()
{
	outNormal = vec4(dataIn.Normal, 1.0f);

    outAlbedo = dataIn.material.Albedo * texture(uAlbedoTexture, dataIn.TexCoord);

	float metallic = texture(uMetallnessTexture, dataIn.TexCoord).r;
	float roughness = texture(uRoghnessTexture, dataIn.TexCoord).r;
	vec2 roughnessMetallness = vec2(dataIn.material.Roughness * roughness, dataIn.material.Metallic * metallic);
	outRoughnessMetallness = roughnessMetallness;
	outEmissive = dataIn.material.Emissive;

}