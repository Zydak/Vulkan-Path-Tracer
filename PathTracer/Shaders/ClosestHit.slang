import RTCommon;
import Material;
import Surface;
import Defines;
import Sampler;
import Volume;

[[vk::binding(1, 0)]] RaytracingAccelerationStructure uTopLevelAS;

[[vk::binding(2, 0)]] ConstantBuffer<UniformBuffer> uUBO;

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
[[vk::binding(10, 0)]] SamplerState uTextureSampler;
[[vk::binding(11, 0)]] StructuredBuffer<CPUMaterial, ScalarDataLayout> uMaterials;

[[vk::image_format("r32f")]]
[[vk::binding(12, 0)]] Texture2DArray uReflectionLookupTable;
[[vk::image_format("r32f")]]
[[vk::binding(13, 0)]] Texture2DArray uRefractionLookupTableHitFromOutside;
[[vk::image_format("r32f")]]
[[vk::binding(14, 0)]] Texture2DArray uRefractionLookupTableHitFromInside;

[[vk::image_format("rgba32f")]]
[[vk::binding(15, 0)]] Texture2D uEnvMapTexture;

// Alias map is used to efficiently select texels from env map based on importance.
[[vk::binding(16, 0)]] StructuredBuffer<AliasMapEntry> uEnvAliasMap;

[[vk::binding(17, 0)]] StructuredBuffer<Volume, ScalarDataLayout> uVolumes;

[[vk::binding(18, 0)]] SamplerState uLookupTableSampler;

float2 getSphericalUv(float3 v)
{
    float gamma = asin(-v.y);
    float theta = atan2(v.z, v.x);

    float2 uv = float2(theta * M_1_OVER_PI * 0.5F, gamma * M_1_OVER_PI) + 0.5F;
    return uv;
}

[shader("closesthit")]
void Main(inout Payload payload, in float2 attrib)
{
    // Reset values from previous hit
    payload.BxDF = float3(1.0f, 1.0f, 1.0f);
    payload.PDF = 1.0f;
    payload.Emitted = float3(0.0f, 0.0f, 0.0f);

    // ---------------------------------------------------------------------------------
    //
    // Initialize Geometry Data
    //

    #ifndef USE_RAY_QUERIES
    {
        // I'm not sure if it's possible to use multiple hit shaders like it is with miss shaders, so I also use this shader
        // to query the distance to surface when ray queries are disabled.
        if (payload.QueryDistance)
        {
            payload.DistanceToSurface = RayTCurrent();
            return;
        }
    }
    #endif

    float3 barycentrics = float3(1.0f - attrib.x - attrib.y, attrib.x, attrib.y);
    uint geometryIndex = InstanceIndex();

    Surface surface;
    // AMD requires NonUniformResourceIndex
    surface.Initialize(
        uVertices[NonUniformResourceIndex(geometryIndex)],
        uIndices[NonUniformResourceIndex(geometryIndex)],
        barycentrics,
        uTextureSampler,
        uNormalTextures[NonUniformResourceIndex(geometryIndex)]
    );

    Material material;
    material.Initialize(
        uMaterials[NonUniformResourceIndex(geometryIndex)],
        surface,
        uTextureSampler,
        uLookupTableSampler,
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
    // Handle Volume Inside Mesh
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
            payload.BxDF *= exp(-(1.0f - material.Properties.MediumColor.rgb) * material.Properties.MediumDensity * geometryHitDistance);
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
                payload.BxDF *= payload.MediumColor.rgb;

                // Return from hit shader. It doesn't terminate the path, it goes back to raygen and starts a new path
                // and since the input.Payload.InMedium is still set we'll end up back here and simulate the next event
                return;
            }
        }
    }

    // ---------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------
    //
    // Importance Sample Env map
    //

    float3 toEnvMapDirectionWorld;
    float3 toEnvMapDirectionTangent;
    float4 envMapValue;
    bool canHitEnvMap = false;
    #ifdef ENABLE_ENV_MAP_MIS
    {
        payload.Sampler.ImportanceSampleEnvMap(uEnvMapTexture, uTextureSampler, uEnvAliasMap, uUBO.EnvMapRotationAzimuth, uUBO.EnvMapRotationAltitude, toEnvMapDirectionWorld, envMapValue);
        envMapValue.rgb *= uUBO.EnvironmentIntensity;

        float azimuth = uUBO.EnvMapRotationAzimuth / 180.0f * M_PI; // Convert degrees to radians
        float altitude = uUBO.EnvMapRotationAltitude / 180.0f * M_PI; // Convert degrees to radians
        toEnvMapDirectionWorld = Rotate(toEnvMapDirectionWorld, float3(1.0f, 0.0f, 0.0f), altitude);
        toEnvMapDirectionWorld = Rotate(toEnvMapDirectionWorld, float3(0.0f, 1.0f, 0.0f), azimuth);

        toEnvMapDirectionTangent = surface.WorldToTangent(toEnvMapDirectionWorld);
        canHitEnvMap = toEnvMapDirectionTangent.z > 0.0f;

        if (canHitEnvMap)
        {
            canHitEnvMap = !DoesRayIntersectWithAS(uTopLevelAS, surface.GetWorldPos() + surface.GetNormal() * 0.00001f, toEnvMapDirectionWorld);
        }
    }
    #endif

    // ---------------------------------------------------------------------------------
    //
    // Importance Sample BxDFs
    //

    float3 V = normalize(-WorldRayDirection());

    // All computations are done in tangent space
    V = surface.WorldToTangent(V);

    // Sample the microsurface
    float3 H = payload.Sampler.GGXSampleAnisotopic(V, material.Ax, material.Ay);

    // Choose sampling probabilities for each lobe
    float FDielectric = material.DielectricFresnel(dot(V, H));
    
    // Sample and evaluate scattering
    BxDFSample scatterSample = material.SampleBSDF(payload.Sampler, V, H, FDielectric);
    bool wasRefracted = scatterSample.L.z < 0.0f;

    if (wasRefracted && surface.WasHitFromInside())
    {
        payload.InMedium = false;
    }
    else if (wasRefracted && !surface.WasHitFromInside())
    {
        payload.InMedium = true;
        payload.MediumColor = material.Properties.MediumColor;
        payload.MediumEmissiveColor = material.Properties.MediumEmissiveColor;
        payload.MediumAnisotropy = material.Properties.MediumAnisotropy;
        payload.MediumDensity = material.Properties.MediumDensity;
    }

    // Evaluate environment map direction
    BxDFEval envMapDirectionEval = BxDFEval(0, 0);
    #ifdef ENABLE_ENV_MAP_MIS
    if (canHitEnvMap)
    {
        envMapDirectionEval = material.EvaluateBSDF(V, H, toEnvMapDirectionTangent, FDielectric);
    }
    #endif

    // ---------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------
    //
    // Write Payload
    //

    // Everything is calculated in tangent space, so at the end we have to convert to world space
    const float3 newDir = surface.TangentToWorld(scatterSample.L);

    payload.Emitted = float3(material.Properties.EmissiveColor);
    // Offset origin slightly to avoid self-intersection on the next event
    payload.Origin = surface.GetWorldPos() + surface.GetNormal() * (-0.00001f * (float)wasRefracted + 0.00001f * (float)(!wasRefracted));
    payload.Direction = newDir;

    payload.BxDF *= scatterSample.BxDF;
    payload.PDF *= scatterSample.PDF;

    #ifdef ENABLE_ENV_MAP_MIS
    {
        const float envMapPDF = envMapValue.a;
        const float3 envMapColor = envMapValue.rgb;
        if (canHitEnvMap)
        {
            // Transmittance along the ray has to be accounted for so that volumes will cast shadows
            float transmittance = 1.0f;
            for (uint i = 0; i < uUBO.VolumesCount; i++)
            {
                const Volume volume = uVolumes[i];
                VolumeIntersection intersection = volume.IntersectWithRay(payload.Origin, toEnvMapDirectionWorld);

                if (volume.IsPointInside(payload.Origin))
                {
                    intersection.HitPointNear = 0.0f;
                }

                float pathThroughVolumeLength = intersection.HitPointFar - intersection.HitPointNear;
                transmittance *= exp(-volume.GetDensity() * pathThroughVolumeLength);
            }

            if (envMapValue.a > 0.0f && envMapDirectionEval.PDF > 0.0f)
            {
                payload.Emitted += (envMapDirectionEval.BxDF * transmittance * envMapValue.rgb / envMapValue.a) * PowerHeuristics(envMapValue.a, envMapDirectionEval.PDF);
            }
        }
    }
    #endif

    // Invalid samples have to be discarded
    bool isInvalid = scatterSample.PDF <= 0.0f;
    payload.Depth = MAX_DEPTH * (isInvalid) + (payload.Depth + 1 * (!isInvalid));

    // ---------------------------------------------------------------------------------
}