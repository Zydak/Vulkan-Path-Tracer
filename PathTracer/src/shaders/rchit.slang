import rtcommon;
import Sampler;
import Surface;
import Payload;
import Material;

struct Input
{
    [[vk::location(0)]] RTPayload Payload;
}

[[vk::binding(0, 0)]] RaytracingAccelerationStructure uTopLevelAS;

[[vk::binding(1, 1)]] Sampler2D uEnvironmentMap;
[[vk::binding(2, 1)]] StructuredBuffer<EnvAccel> uAccels;

[[vk::binding(2, 0)]]
StructuredBuffer<MeshAddress> uMeshAddresses;

[[vk::binding(3, 0)]]
StructuredBuffer<CPUMaterial, Std430DataLayout> uMaterials;

[[vk::binding(4, 0)]] Sampler2D uAlbedoTextures[];
[[vk::binding(5, 0)]] Sampler2D uNormalTextures[];
[[vk::binding(6, 0)]] Sampler2D uRoghnessTextures[];
[[vk::binding(7, 0)]] Sampler2D uMetallnessTextures[];

[[vk::push_constant]] PushConstant push;

[shader("closesthit")]
void main(inout Input input, in float2 attribs)
{
    // Prepare Surface
    Surface surface;
    surface.Prepare(uMeshAddresses[InstanceIndex()], attribs, uNormalTextures[InstanceIndex()]);

    // Prepare Material
    Material material;
    material.LoadFromCPU(
        uMaterials[InstanceIndex()],
        surface,
        uAlbedoTextures[InstanceIndex()],
        uRoghnessTextures[InstanceIndex()],
        uMetallnessTextures[InstanceIndex()]
    );
    material.EmissiveColor = uMaterials[InstanceIndex()].EmissiveColor;

    surface.RotateTangents(material.AnisotropyRotation);

    float3 V = -WorldRayDirection();
    V = surface.WorldToTangent(V);

    bool hitFromTheInside = material.Eta > 1.0f;

    if (input.Payload.GBufferPass)
    {
        input.Payload.GBufferColor = material.Color.xyz;
        input.Payload.GBufferNormal = surface.GetNormal();
    
        return;
    }

    // Medium

    if (input.Payload.InMedium)
    {
        float geometryHitDistance = length(input.Payload.RayOrigin - surface.GetWorldPos());
        // Scattering Occured
        if (input.Payload.MediumAnisotropy == 1.0f)
        {
            // Use Beer's law
            input.Payload.Weight *= exp(-(1.0f - material.MediumColor.rgb) * material.MediumDensity * geometryHitDistance);
        }
        else
        {
            // Simulate scattering events
            float scatterDistance = -log(input.Payload.Sampler.UniformFloat()) / input.Payload.MediumDensity;
            
            if (scatterDistance < geometryHitDistance)
            {
                input.Payload.RayOrigin = input.Payload.RayOrigin + (scatterDistance * input.Payload.RayDirection);

                input.Payload.RayDirection = input.Payload.Sampler.SampleHenyeyGreenstein( input.Payload.MediumAnisotropy, input.Payload.RayDirection);

                input.Payload.Weight *= input.Payload.MediumColor.rgb;

                return;
            }
        }
    }

    // Direct Lighting of the surface

    float3 dirToLight;
    float4 envColor; // w = pdf
    input.Payload.Sampler.SampleImportanceEnvMap(uEnvironmentMap, uAccels, push.EnvAzimuth, push.EnvAltitude, dirToLight, envColor);

    float3 BSDFSurfaceToLight;
    float PDFSurfaceToLight;

    bool canHit = dot(dirToLight, surface.GetNormal()) > 0.0f;

    if (canHit)
    {
        RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
        query.__rayQueryInitializeEXT(uTopLevelAS, RAY_FLAG_FORCE_OPAQUE, 0xFF, surface.GetWorldPos(), 0.0001, dirToLight, 1000000);
        query.Proceed();
        if (query.CommittedStatus() != COMMITTED_NOTHING)
        {
            canHit = false;
        }
    }

    float F0 = (1.0f - material.Eta) / (1.0f + material.Eta);
    F0 *= F0;

    float diffuseProbability = (1.0 - material.Metallic) * (1.0 - material.Transparency);
    float metallicProbability = material.Metallic;
    float dielectricProbability = (1.0 - material.Metallic) * F0 * (1.0 - material.Transparency);
    float glassProbability = (1.0 - material.Metallic) * material.Transparency;

    float probabilitySum = diffuseProbability + metallicProbability + dielectricProbability + glassProbability;
    diffuseProbability /= probabilitySum;
    metallicProbability /= probabilitySum;
    dielectricProbability /= probabilitySum;
    glassProbability /= probabilitySum;

    float x1 = input.Payload.Sampler.UniformFloat();

    bool refracted = false;
    BSDFSample sample;
    if (x1 < diffuseProbability)
    {
        sample = material.SampleDiffuse(input.Payload.Sampler, V);
        BSDFSurfaceToLight = material.EvaluateDiffuse(V, surface.WorldToTangent(dirToLight), PDFSurfaceToLight);
    }
    else if (x1 < metallicProbability + diffuseProbability)
    {
        sample = material.SampleMetallic(input.Payload.Sampler, V);
        BSDFSurfaceToLight = material.EvaluateMetallic(V, surface.WorldToTangent(dirToLight), PDFSurfaceToLight);
    }
    else if (x1 < diffuseProbability + metallicProbability + dielectricProbability)
    {
        sample = material.SampleDielectric(input.Payload.Sampler, V);
        BSDFSurfaceToLight = material.EvaluateDielectricReflection(V, surface.WorldToTangent(dirToLight), 1.0, PDFSurfaceToLight);
    }
    else if (x1 <= diffuseProbability + metallicProbability + dielectricProbability + glassProbability)
    {
        float F;
        sample = material.SampleGlass(input.Payload.Sampler, V, refracted, F);

        if (refracted)
            BSDFSurfaceToLight = material.EvaluateDielectricRefraction(V, surface.WorldToTangent(dirToLight), F, PDFSurfaceToLight);
        else
            BSDFSurfaceToLight = material.EvaluateDielectricReflection(V, surface.WorldToTangent(dirToLight), F, PDFSurfaceToLight);

        if (refracted && hitFromTheInside)
        {
            // Hit from the inside, ray exited the object
            input.Payload.InMedium = false;
        }
        else if (refracted && !hitFromTheInside)
        {
            // Hit from the outside, ray went into the object
            input.Payload.InMedium = true;
            input.Payload.MediumDensity = material.MediumDensity;
            input.Payload.MediumAnisotropy = material.MediumAnisotropy;
            input.Payload.MediumColor = material.MediumColor.rgb;
        }
    }

    if (!refracted && sample.L.z <= 0.0f)
    {
        input.Payload.Weight = float3(-1.0);
        input.Payload.Depth = DEPTH_INFINITE;
        return;
    }

    if (refracted && sample.L.z >= 0.0f)
    {
        input.Payload.Weight = float3(-1.0);
        input.Payload.Depth = DEPTH_INFINITE;
        return;
    }

    if (sample.PDF <= 0.0f)
    {
        input.Payload.Weight = float3(-1.0);
        input.Payload.Depth = DEPTH_INFINITE;
        return;
    }

    input.Payload.RayDirection = surface.TangentToWorld(sample.L);
    input.Payload.RayOrigin = surface.GetWorldPos();
    input.Payload.Weight *= sample.BSDF / sample.PDF;
    input.Payload.HitValue = material.EmissiveColor.rgb * material.EmissiveColor.a;

    if (canHit)
    {
        float lightWeight = PowerHeuristic(envColor.w, PDFSurfaceToLight);

        input.Payload.HitValue += dot(surface.GetNormal(), dirToLight) * (envColor.rgb / envColor.w) * BSDFSurfaceToLight * lightWeight;
    }

    //input.Payload.HitValue = (surface.GetNormal() + float3(1.0f)) * float3(0.5f);
}