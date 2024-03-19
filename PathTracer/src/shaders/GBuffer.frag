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
	mat3 TBN;
	vec2 TexCoord;
};

layout (location = 0) in DataIn dataIn;

void main()
{
#ifdef USE_NORMAL_MAPS
	vec3 normalMapVal = texture(uNormalTexture, dataIn.TexCoord).xyz * 2.0f - 1.0f;
	normalMapVal = normalize(dataIn.TBN * normalMapVal);

	// no idea why but sometimes it's just nan
	if(isnan(normalMapVal.x) || isnan(normalMapVal.y) || isnan(normalMapVal.z))
	{
		normalMapVal = vec3(0.5f, 0.5f, 1.0f);
	}
#else
	vec3 normalMapVal = vec3(0.5f, 0.5f, 1.0f) * 2.0f - 1.0f;
	normalMapVal = normalize(dataIn.TBN * normalMapVal);
#endif

	outNormal = 0.5 * (vec4(normalMapVal, 1.0f) + 1.0f);

#ifdef USE_ALBEDO
    outAlbedo = dataIn.material.Albedo * texture(uAlbedoTexture, dataIn.TexCoord);
#else
    outAlbedo = vec4(0.5f);
#endif

	float metallic = texture(uMetallnessTexture, dataIn.TexCoord).r;
	float roughness = texture(uRoghnessTexture, dataIn.TexCoord).r;
	vec2 roughnessMetallness = vec2(dataIn.material.Roughness * roughness, dataIn.material.Metallic * metallic);
	outRoughnessMetallness = roughnessMetallness;
	outEmissive = dataIn.material.Emissive;

}