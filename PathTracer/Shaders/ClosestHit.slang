import RTCommon;
import Material;
import Surface;

[[vk::binding(3, 0)]] StructuredBuffer<Vertex, ScalarDataLayout> uVertices[];
[[vk::binding(4, 0)]] StructuredBuffer<uint> uIndices[];

[[vk::binding(5, 0)]] Texture2D uBaseColorTextures[];
[[vk::binding(6, 0)]] Texture2D uNormalTextures[];
[[vk::binding(7, 0)]] Texture2D uRoughnessTextures[];
[[vk::binding(8, 0)]] Texture2D uMetallicTextures[];
[[vk::binding(9, 0)]] Texture2D uEmissiveTextures[];
[[vk::binding(10, 0)]] SamplerState uSampler;
[[vk::binding(11, 0)]] StructuredBuffer<CPUMaterial, ScalarDataLayout> uMaterials;

[shader("closesthit")]
void Main(inout Payload payload, in float2 attrib)
{
    float3 barycentrics = float3(1.0f - attrib.x - attrib.y, attrib.x, attrib.y);
    uint geometryIndex = GeometryIndex();

    Surface surface;
    surface.Initialize(uVertices[NonUniformResourceIndex(geometryIndex)], uIndices[NonUniformResourceIndex(geometryIndex)], barycentrics, -payload.Direction);

    Material material;
    material.Initialize(
        uMaterials[NonUniformResourceIndex(geometryIndex)],
        surface,
        uSampler,
        uBaseColorTextures[NonUniformResourceIndex(geometryIndex)],
        uRoughnessTextures[NonUniformResourceIndex(geometryIndex)],
        uMetallicTextures[NonUniformResourceIndex(geometryIndex)],
        uEmissiveTextures[NonUniformResourceIndex(geometryIndex)]
    );

    // Generate random direction on the hemisphere around the normal
    BxDFSample sample = material.SampleDiffuse(payload.Sampler, -payload.Direction);

    // Everything is calculated in tangent space, so at the end we have to convert to world space
    const float3 newDir = surface.TangentToWorld(sample.L);

    payload.SurfaceColor = float4(sample.BxDF / sample.PDF, 1.0f);
    payload.LightColor = float4(material.Properties.EmissiveColor, 1.0f);
    payload.Direction = newDir;
    payload.Origin = surface.GetWorldPos() + surface.GetNormal() * 0.00001f; // Offset origin slightly to avoid self-intersection
    payload.Depth += 1;
}