import rtcommon;
import Sampler;
import Surface;
import Payload;
import Material;
import Volume;

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

[[vk::binding(12, 0)]] StructuredBuffer<Volume> uVolumes;

[[vk::push_constant]] PushConstant push;

[shader("closesthit")]
void main(inout Input input, in float2 attribs)
{
    // Prepare Surface
    Surface surface;
    surface.Prepare(uMeshAddresses[InstanceIndex()], attribs, uNormalTextures[NonUniformResourceIndex(InstanceIndex())]);

    // Prepare Material
    Material material;
    material.LoadFromCPU(
        uMaterials[NonUniformResourceIndex(InstanceIndex())],
        surface,
        uAlbedoTextures[NonUniformResourceIndex(InstanceIndex())],
        uRoghnessTextures[NonUniformResourceIndex(InstanceIndex())],
        uMetallnessTextures[NonUniformResourceIndex(InstanceIndex())]
    );

    surface.RotateTangents(material.AnisotropyRotation);

    float3 V = -WorldRayDirection();
    V = surface.WorldToTangent(V);

    bool hitFromTheInside = material.Eta > 1.0f;

    // See the end of rgen.slang
    if (input.Payload.GBufferPass)
    {
        input.Payload.GBufferColor = material.Color.xyz;
        input.Payload.GBufferNormal = surface.GetNormal();
    
        return;
    }

    // Medium
    if (input.Payload.InMedium)
    {
        // Get distance to the geometry from prev hit point
        float geometryHitDistance = length(input.Payload.RayOrigin - surface.GetWorldPos());

        if (input.Payload.MediumAnisotropy == 1.0f)
        {
            // If the anisotropy equals 1 we can skip simulating the scattering events and just use Beer's law instead which is way faster
            // and gives the same result.
            input.Payload.Weight *= exp(-(1.0f - material.MediumColor.rgb) * material.MediumDensity * geometryHitDistance);
        }
        else
        {
            // If the anisotropy isn't 1, ray can bounce in different directions inside a medium. So in that case scattering
            // events have to be simulated

            // Randomly choose scattering distance based on medium density
            float scatterDistance = -log(input.Payload.Sampler.UniformFloat()) / input.Payload.MediumDensity;
            
            if (scatterDistance < geometryHitDistance)
            {
                // If the scattering distance is smaller than closest geometry the ray is still inside the medium.

                // Choose new origin and idrection
                input.Payload.RayOrigin = input.Payload.RayOrigin + (scatterDistance * input.Payload.RayDirection);
                input.Payload.RayDirection = input.Payload.Sampler.SampleHenyeyGreenstein( input.Payload.RayDirection, input.Payload.MediumAnisotropy);

                // Change weight and hit value
                input.Payload.Weight *= input.Payload.MediumColor.rgb;

                // Return from hit shader. It doesn't terminate the path, it goes back to raygen and starts a new path
                // and since the input.Payload.InMedium is still set we'll end up back here and simulate the next event
                return;
            }
        }
    }

    // Direct Lighting of the surface (only Environment map has direct lighting, There is no importance sampling of emissive meshes)

    // Randomly sample the environment map
    float3 dirToLight;
    float4 envColor; // w = pdf
    input.Payload.Sampler.SampleImportanceEnvMap(uEnvironmentMap, uAccels, push.EnvAzimuth, push.EnvAltitude, dirToLight, envColor);

    float3 BSDFSurfaceToLight;
    float PDFSurfaceToLight;

    // Check whether it can be hit or not
    bool canHit = dot(dirToLight, surface.GetNormal()) > 0.0f;
    if (canHit)
    {
        RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
        query.__rayQueryInitializeEXT(uTopLevelAS, RAY_FLAG_FORCE_OPAQUE, 0xFF, surface.GetWorldPos(), 0.0001, dirToLight, 1000000);
        query.Proceed();
        if (query.CommittedStatus() != COMMITTED_NOTHING)
        {
            canHit = false; // If an intersection is found on the way that means the shaded point is obscured by geometry
        }
    }

    // Choose sampling probabilities for each lobe
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

    // Sample the lobes based on their probabilities
    bool refracted = false;
    BSDFSample sample;
    if (x1 < diffuseProbability)
    {
        // Diffuse lobe

        sample = material.SampleDiffuse(input.Payload.Sampler, V);
        BSDFSurfaceToLight = material.EvaluateDiffuse(V, surface.WorldToTangent(dirToLight), PDFSurfaceToLight);
    }
    else if (x1 < diffuseProbability + metallicProbability)
    {
        // Metallic lobe

        sample = material.SampleMetallic(input.Payload.Sampler, V);
        BSDFSurfaceToLight = material.EvaluateMetallic(V, surface.WorldToTangent(dirToLight), PDFSurfaceToLight);
    }
    else if (x1 < diffuseProbability + metallicProbability + dielectricProbability)
    {
        // Dielectric lobe

        sample = material.SampleDielectric(input.Payload.Sampler, V);
        BSDFSurfaceToLight = material.EvaluateDielectricReflection(V, surface.WorldToTangent(dirToLight), 1.0, PDFSurfaceToLight);
    }
    else if (x1 <= diffuseProbability + metallicProbability + dielectricProbability + glassProbability)
    {
        // Glass lobe

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

    // Terminate the path if the direction goes below (or above for refraction) the surface.
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

    // Terminate the path if the PDF is 0
    if (sample.PDF <= 0.0f)
    {
        input.Payload.Weight = float3(-1.0);
        input.Payload.Depth = DEPTH_INFINITE;
        return;
    }

    // Save the values to the payload for raygen shader to read
    input.Payload.RayDirection = surface.TangentToWorld(sample.L);
    input.Payload.RayOrigin = surface.GetWorldPos();
    input.Payload.Weight *= sample.BSDF / sample.PDF;
    input.Payload.HitValue += material.EmissiveColor.rgb * material.EmissiveColor.a;

    // If the path to the light isn't obscured then sample the light
    if (canHit)
    {
        float lightWeight = PowerHeuristic(envColor.w, PDFSurfaceToLight);

        // Whether the path is obscured by a volume has to be checked for separately from the geometry.
        float3 volumeAbsorption = float3(1.0f);

        // Iterate through all volumes
        for (int i = 0; i < push.VolumesCount; i++)
        {
            Volume volume = uVolumes[i];

            // Check collision positions with volume
            float3 hitPosNear;
            float3 hitPosFar;
            volume.Aabb.IntersectRay(surface.GetWorldPos(), dirToLight, hitPosNear, hitPosFar);

            bool isInside = volume.Aabb.IsInsideAABB(surface.GetWorldPos());
            if (isInside)
                hitPosNear = surface.GetWorldPos();

            if (VectorEqual(hitPosNear, FLT_MAX) || VectorEqual(hitPosFar, FLT_MAX))
                continue;

            float volumeWidth = length(hitPosFar - hitPosNear);

            // Calculate volume absorption based on Beer's law
            volumeAbsorption *= exp(-volume.ScatteringCoefficient * volumeWidth);
        }

        // Add the light contribution
        input.Payload.HitValue += volumeAbsorption * dot(surface.GetNormal(), dirToLight) * (envColor.rgb / envColor.w) * BSDFSurfaceToLight * lightWeight;
    }

    // Debug output for normals
    //input.Payload.HitValue = (surface.GetNormal() + float3(1.0f)) * float3(0.5f);
}