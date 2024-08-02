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

void CreateCoordinateSystem(in vec3 N, out vec3 Nt, out vec3 Nb)
{
    if(abs(N.x) > abs(N.y))
        Nt = vec3(N.z, 0, -N.x) / sqrt(N.x * N.x + N.z * N.z);
    else
        Nt = vec3(0, -N.z, N.y) / sqrt(N.y * N.y + N.z * N.z);
    Nb = cross(N, Nt);
}

vec3 TangentToWorld(vec3 T, vec3 B, vec3 N, vec3 V)
{
    return V.x * T + V.y * B + V.z * N;
}

void main()
{
	//outNormal = vec4(dataIn.Normal + 0.5f * 0.5f, 1.0f);

	vec3 Tangent;
	vec3 Bitangent;
	CreateCoordinateSystem(dataIn.Normal, Tangent, Bitangent);

	vec3 texNorm = texture(uNormalTexture, dataIn.TexCoord).rgb;
	texNorm = texNorm * 2.0f - 1.0f;
	texNorm = normalize(TangentToWorld(Tangent, Bitangent, dataIn.Normal, texNorm));

	outNormal = vec4((vec3(-texNorm.x, -texNorm.y, texNorm.z) + 1.0f) * 0.5f, 1.0f);

    outAlbedo = dataIn.material.Albedo * texture(uAlbedoTexture, dataIn.TexCoord);

	float metallic = texture(uMetallnessTexture, dataIn.TexCoord).r;
	float roughness = texture(uRoghnessTexture, dataIn.TexCoord).r;
	vec2 roughnessMetallness = vec2(dataIn.material.Roughness * roughness, dataIn.material.Metallic * metallic);
	outRoughnessMetallness = roughnessMetallness;
	outEmissive = dataIn.material.Emissive;

}