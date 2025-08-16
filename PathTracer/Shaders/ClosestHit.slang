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
    surface.Initialize(uVertices[NonUniformResourceIndex(geometryIndex)], uIndices[NonUniformResourceIndex(geometryIndex)], barycentrics);

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

    // Choose sampling probabilities for each lobe
    float F0 = (1.0 - material.Eta) / (1.0f + material.Eta);
    F0 *= F0;

    float diffuseProbability = (1.0 - material.Properties.Metallic) * (1.0 - material.Properties.Transmission);
    float metallicProbability = material.Properties.Metallic;
    float dielectricProbability = (1.0 - material.Properties.Metallic) * F0 * (1.0 - material.Properties.Transmission);
    float glassProbability = (1.0 - material.Properties.Metallic) * material.Properties.Transmission;

    // Normalize probabilities
    float probabilitySum = diffuseProbability + metallicProbability + dielectricProbability + glassProbability;
    diffuseProbability /= probabilitySum;
    metallicProbability /= probabilitySum;
    dielectricProbability /= probabilitySum;
    glassProbability /= probabilitySum;

    float x1 = payload.Sampler.UniformFloat();

    float3 V = normalize(-WorldRayDirection());

    // All computations are done in tangent space
    V = surface.WorldToTangent(V);

    BxDFSample sample;
    bool wasRefracted;
    if (x1 < diffuseProbability)
    {
        // Sample diffuse lobe
        sample = material.SampleDiffuse(payload.Sampler, V);
    }
    else if (x1 < diffuseProbability + metallicProbability)
    {
        // Sample metallic lobe
        sample = material.SampleMetallic(payload.Sampler, V);
    }
    else if (x1 < diffuseProbability + metallicProbability + dielectricProbability)
    {
        // Sample dielectric lobe
        sample = material.SampleDielectric(payload.Sampler, V);
    }
    else
    {
        // Sample glass lobe
        sample = material.SampleGlass(payload.Sampler, V, wasRefracted);
    }

    // Everything is calculated in tangent space, so at the end we have to convert to world space
    const float3 newDir = surface.TangentToWorld(sample.L);

    payload.SurfaceColor = float4(sample.BxDF / sample.PDF, 1.0f);
    payload.LightColor = float4(material.Properties.EmissiveColor, 1.0f);
    payload.Direction = newDir;

    // Offset origin slightly to avoid self-intersection on the next event
    payload.Origin = surface.GetWorldPos() + surface.GetNormal() * (-0.0001f * (float)wasRefracted + 0.0001f * (float)(!wasRefracted));

    bool isInvalid = (!wasRefracted && dot(newDir, surface.GetNormal()) < 0.0f) || sample.PDF < 0.0f;
    payload.Depth += 1;//0xFFFFFFFF * (isInvalid) + 1 * (!isInvalid);
}