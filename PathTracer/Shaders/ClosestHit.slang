import RTCommon;
import Material;
import Surface;
import Defines;

[[vk::binding(3, 0)]] StructuredBuffer<Vertex, ScalarDataLayout> uVertices[];
[[vk::binding(4, 0)]] StructuredBuffer<uint> uIndices[];

[[vk::image_format("rgba8")]]
[[vk::binding(5, 0)]] Texture2D uBaseColorTextures[];
[[vk::image_format("rgba8")]]
[[vk::binding(6, 0)]] Texture2D uNormalTextures[];
[[vk::image_format("rgba8")]]
[[vk::binding(7, 0)]] Texture2D uRoughnessTextures[];
[[vk::image_format("rgba8")]]
[[vk::binding(8, 0)]] Texture2D uMetallicTextures[];
[[vk::image_format("rgba8")]]
[[vk::binding(9, 0)]] Texture2D uEmissiveTextures[];
[[vk::binding(10, 0)]] SamplerState uSampler;
[[vk::binding(11, 0)]] StructuredBuffer<CPUMaterial, ScalarDataLayout> uMaterials;

[[vk::image_format("r32f")]]
[[vk::binding(12, 0)]] Texture2DArray uReflectionLookupTable;
[[vk::binding(13, 0)]] Texture2DArray uRefractionLookupTableHitFromOutside;
[[vk::binding(14, 0)]] Texture2DArray uRefractionLookupTableHitFromInside;

[shader("closesthit")]
void Main(inout Payload payload, in float2 attrib)
{
    // ---------------------------------------------------------------------------------
    //
    // Initialize Geometry Data
    //

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
        uEmissiveTextures[NonUniformResourceIndex(geometryIndex)],
        uReflectionLookupTable,
        uRefractionLookupTableHitFromOutside,
        uRefractionLookupTableHitFromInside
    );

    surface.RotateTangents(material.Properties.AnisotropyRotation);

    // ---------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------
    //
    // Handle Volumes
    //

    if (payload.InMedium)
    {
        // Get distance to the geometry from prev hit point
        float geometryHitDistance = length(payload.Origin - surface.GetWorldPos());

        if (payload.MediumAnisotropy == 1.0f)
        {
            // If the anisotropy equals 1 the entire path through volume is perfectly straight line, in that case
            // it's pointless to simulate each scattering event since we know where we'll end up. So just use Beer's law instead which is way faster
            // and gives the same result.
            payload.SurfaceColor *= exp(-(1.0f - material.Properties.MediumColor.rgb) * material.Properties.MediumDensity * geometryHitDistance);
        }
        else
        {
            // If the anisotropy isn't 1, ray can bounce in different directions inside a medium. So in that case scattering
            // events have to be simulated

            // Randomly choose scattering distance based on medium density
            float scatterDistance = -log(payload.Sampler.UniformFloat()) / payload.MediumDensity;
            
            if (scatterDistance < geometryHitDistance)
            {
                // If the scattering distance is smaller than closest geometry the ray is still inside the medium.

                // Choose new origin and idrection
                payload.Origin = payload.Origin + (scatterDistance * payload.Direction);
                payload.Direction = payload.Sampler.SampleHenyeyGreenstein( payload.Direction, payload.MediumAnisotropy);

                // Change weight
                payload.SurfaceColor *= payload.MediumColor.rgb;

                // Return from hit shader. It doesn't terminate the path, it goes back to raygen and starts a new path
                // and since the input.Payload.InMedium is still set we'll end up back here and simulate the next event
                return;
            }
        }
    }

    // ---------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------
    //
    // Sample BxDFs
    //

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
    bool wasRefracted = false;
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

        // Ray left the mesh
        if (wasRefracted && surface.WasHitFromInside())
        {
            payload.InMedium = false;
        }

        // Ray entered the mesh
        if (wasRefracted && !surface.WasHitFromInside())
        {
            payload.InMedium = true;
            payload.MediumDensity = material.Properties.MediumDensity;
            payload.MediumAnisotropy = material.Properties.MediumAnisotropy;
            payload.MediumColor = material.Properties.MediumColor.rgb;
            payload.MediumEmissiveColor = material.Properties.MediumEmissiveColor.rgb;
        }
    }

    // ---------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------
    //
    // Write Payload
    //

    // Everything is calculated in tangent space, so at the end we have to convert to world space
    const float3 newDir = surface.TangentToWorld(sample.L);

    payload.SurfaceColor *= float3(sample.BxDF / sample.PDF);
    payload.LightColor = float3(material.Properties.EmissiveColor);
    payload.Direction = newDir;

    // Offset origin slightly to avoid self-intersection on the next event
    payload.Origin = surface.GetWorldPos() + surface.GetNormal() * (-0.0001f * (float)wasRefracted + 0.0001f * (float)(!wasRefracted));

    // Invalid samples have to be discarded
    bool isInvalid = (wasRefracted && sample.L.z >= 0.0f) || (!wasRefracted && sample.L.z <= 0.0f) || sample.PDF <= 0.0f;
    payload.Depth = MAX_DEPTH * (isInvalid) + (payload.Depth + 1 * (!isInvalid));

    // ---------------------------------------------------------------------------------
}