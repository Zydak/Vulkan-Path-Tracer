import RTCommon;
import Material;
import Surface;
import Defines;
import Sampler;
import Volume;
import Atmosphere;

import Bindings;

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
    uint instanceIndex = InstanceIndex();
    uint materialIndex = uMaterialAndMeshIndices[NonUniformResourceIndex(instanceIndex * 2)];
    uint meshIndex = uMaterialAndMeshIndices[NonUniformResourceIndex(instanceIndex * 2 + 1)];

    Surface surface;
    // AMD requires NonUniformResourceIndex
    surface.Initialize(
        uVertices[NonUniformResourceIndex(meshIndex)],
        uIndices[NonUniformResourceIndex(meshIndex)],
        barycentrics,
        uTextures[NonUniformResourceIndex(uMaterials[NonUniformResourceIndex(materialIndex)].NormalTextureIndex)]
    );

    Material material;
    material.Initialize(
        uMaterials[NonUniformResourceIndex(materialIndex)],
        surface
    );

    surface.RotateTangents(material.Properties.AnisotropyRotation);

    // payload.Emitted = clamp(surface.GetNormal() * 0.5f + 0.5f, 0.0f, 1.0f);
    // payload.Depth = MAX_DEPTH;
    // return;

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

    float3 toSkyDirectionWorld;
    float3 toSkyDirectionTangent;
    float4 skyValue;
    bool canHitSky = false;
    #ifdef ENABLE_SKY_MIS
    {
        payload.Sampler.ImportanceSampleSky(toSkyDirectionWorld, skyValue);

        skyValue.rgb *= uUBO.EnvironmentIntensity;

        toSkyDirectionTangent = surface.WorldToTangent(toSkyDirectionWorld);
        canHitSky = toSkyDirectionTangent.z > 0.0f;

        if (canHitSky)
        {
            canHitSky = !DoesRayIntersectWithAS(uTopLevelAS, surface.GetWorldPos() + surface.GetNormal() * 1e-5, toSkyDirectionWorld);
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
    BxDFEval skyDirectionEval = BxDFEval(0, 0);
    #ifdef ENABLE_SKY_MIS
    if (canHitSky)
    {
        skyDirectionEval = material.EvaluateBSDF(V, H, toSkyDirectionTangent, FDielectric);
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
    payload.Origin = surface.GetWorldPos() + surface.GetNormal() * (-1e-3 * (float)wasRefracted + 1e-3 * (float)(!wasRefracted));
    payload.Direction = newDir;

    payload.BxDF *= scatterSample.BxDF;
    payload.PDF *= scatterSample.PDF;

    #ifdef ENABLE_SKY_MIS
    {
        const float skyMapPDF = skyValue.a;
        const float3 skyColor = skyValue.rgb;
        if (canHitSky)
        {
            // Transmittance along the ray has to be accounted for so that volumes will cast shadows
            int tempCollisionCount = 0;
            float3 transmittance = Volume::CalculateVolumesTransmittance(payload.Sampler, payload.Origin, toSkyDirectionWorld, payload.Depth, tempCollisionCount);

            #ifdef ENABLE_ATMOSPHERE
            {
                if (payload.ColorChannel == -1)
                {
                    // Ray hasn't been split yet, so just sum all color channels
                    transmittance.r *= CalculateTransmittanceThroughAtmosphere(payload.Sampler, payload.Origin, toSkyDirectionWorld, 0).r;
                    transmittance.g *= CalculateTransmittanceThroughAtmosphere(payload.Sampler, payload.Origin, toSkyDirectionWorld, 1).g;
                    transmittance.b *= CalculateTransmittanceThroughAtmosphere(payload.Sampler, payload.Origin, toSkyDirectionWorld, 2).b;
                }
                else
                {
                    // Ray has been split, so only calculate the transmittance for the relevant color channel
                    transmittance *= CalculateTransmittanceThroughAtmosphere(payload.Sampler, payload.Origin, toSkyDirectionWorld, payload.ColorChannel);
                }
            }
            #endif

            if (skyValue.a > 0.0f && skyDirectionEval.PDF > 0.0f)
            {
                payload.Emitted += (skyDirectionEval.BxDF * transmittance * skyColor / skyMapPDF) * PowerHeuristics(skyMapPDF, skyDirectionEval.PDF);
            }
        }
    }
    #endif

    // Invalid samples have to be discarded
    bool isInvalid = scatterSample.PDF <= 0.0f;
    payload.Depth = MAX_DEPTH * (isInvalid) + (payload.Depth + 1 * (!isInvalid));
    // ---------------------------------------------------------------------------------
}