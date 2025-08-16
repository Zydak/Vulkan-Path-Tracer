import RTCommon;

struct Vertex
{
    float3 Position;
    float3 Normal;
    float2 TexCoord;
};

[[vk::binding(3, 0)]] StructuredBuffer<Vertex, ScalarDataLayout> uVertices[];
[[vk::binding(4, 0)]] StructuredBuffer<uint> uIndices[];

[[vk::binding(5, 0)]] Texture2D uBaseColorTextures[];
[[vk::binding(6, 0)]] Texture2D uNormalColorTextures[];
[[vk::binding(7, 0)]] Texture2D uRoughnessTextures[];
[[vk::binding(8, 0)]] Texture2D uMetallicTextures[];
[[vk::binding(9, 0)]] Texture2D uEmissiveTextures[];
[[vk::binding(10, 0)]] SamplerState uSampler;
[[vk::binding(11, 0)]] StructuredBuffer<Material, ScalarDataLayout> uMaterials;

[shader("closesthit")]
void Main(inout Payload payload, in float2 attrib)
{
    float3 barycentrics = float3(1.0f - attrib.x - attrib.y, attrib.x, attrib.y);
    uint primitiveIndex = PrimitiveIndex();

    uint geometryIndex = GeometryIndex();

    // AMD requires NonUniformResourceIndex
    const uint i1 = uIndices[NonUniformResourceIndex(geometryIndex)][primitiveIndex * 3 + 0];
    const uint i2 = uIndices[NonUniformResourceIndex(geometryIndex)][primitiveIndex * 3 + 1];
    const uint i3 = uIndices[NonUniformResourceIndex(geometryIndex)][primitiveIndex * 3 + 2];

    const Vertex v1 = uVertices[NonUniformResourceIndex(geometryIndex)][i1];
    const Vertex v2 = uVertices[NonUniformResourceIndex(geometryIndex)][i2];
    const Vertex v3 = uVertices[NonUniformResourceIndex(geometryIndex)][i3];

    float3 position = v1.Position * barycentrics.x + v2.Position * barycentrics.y + v3.Position * barycentrics.z;
    position = mul(ObjectToWorld3x4(), float4(position, 1.0f)).xyz;

    // Don't use model normals, they're often transformed which causes rays to go under the geometry
    //float3 normal = normalize(v1.Normal * barycentrics.x + v2.Normal * barycentrics.y + v3.Normal * barycentrics.z);

    // Instead compute geometric normals
    float3 normal = normalize(cross(v2.Position - v1.Position, v3.Position - v1.Position));
    normal = mul(normal, WorldToObject()).xyz;

    if (dot(normal, payload.Direction) > 0.0f)
    {
        // Sometimes the geometry is flat (e.g. a 2D plane), it has only one face (and one normal direction).
        // If that's the case then flip the normal and act as if it has two faces.
        normal = -normal;
    }

    const float2 texCoord = v1.TexCoord * barycentrics.x + v2.TexCoord * barycentrics.y + v3.TexCoord * barycentrics.z;

    float3 color = uBaseColorTextures[NonUniformResourceIndex(geometryIndex)].Sample(uSampler, texCoord).rgb;
    float3 emissiveColor = uEmissiveTextures[NonUniformResourceIndex(geometryIndex)].Sample(uSampler, texCoord).rgb;
    //color = float3(0.8f);

    // Generate random direction on the hemisphere around the normal
    float3 newDirection = payload.Sampler.RandomSphereVec();
    if (dot(newDirection, normal) < 0.0f)
    {
        newDirection = -newDirection; // Ensure the direction is in the same hemisphere as the normal
    }

    Material material = uMaterials[NonUniformResourceIndex(geometryIndex)];

    payload.SurfaceColor = float4(color * material.BaseColor, 1.0f);
    payload.LightColor = float4(emissiveColor * material.EmissiveColor, 1.0f);
    payload.Direction = newDirection;
    payload.Origin = position + normal * 0.00001f; // Offset origin slightly to avoid self-intersection
    payload.Depth += 1;
}