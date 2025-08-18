//
// Reference [https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf]
//

import Material;
import Sampler;

struct Push
{
    uint Seed;
    uint SampleCount;

    uint TableSizeX;
    uint TableSizeY;
    uint TableSizeZ;

    bool AboveSurface;
}

[[vk::binding(0, 0)]] RWStructuredBuffer<float> uTable;

[[vk::push_constant]] Push push;

[shader("compute")]
[numthreads(8, 8, 1)]
void Main(uint3 threadId: SV_DispatchThreadID)
{
    if (threadId.x >= push.TableSizeX || threadId.y >= push.TableSizeY || threadId.z >= push.TableSizeZ)
        return;

    uint index = threadId.x + threadId.y * push.TableSizeX + threadId.z * push.TableSizeX * push.TableSizeY;

    Sampler sampler = Sampler(threadId.y + threadId.x * threadId.x + push.Seed);

    // Set all values
    float viewCosine = clamp(float(threadId.x) / push.TableSizeX, 0.001, 0.999);
    float roughness = clamp(float(threadId.y) / push.TableSizeY, 0.001, 1.0);
    float anisotropy = float(threadId.z) / push.TableSizeZ;
    const float aspect = sqrt(1.0 - sqrt(anisotropy) * 0.9);
    float ax = max(0.001, roughness / aspect);
    float ay = max(0.001, roughness * aspect);

    // Prepare material
    Material material;
    material.Properties.Anisotropy = anisotropy;
    material.Properties.Roughness = roughness;
    material.Properties.BaseColor = float3(1.0, 1.0, 1.0);
    material.Ax = ax;
    material.Ay = ay;

    float finalValue = 0.0;
    for (int i = 0; i < push.SampleCount; i++)
    {
        // Generate random view dir
        float xyMagnitudeSquared = 1.0 - viewCosine * viewCosine;
        float phiV = sampler.UniformFloat();
        float x = sqrt(xyMagnitudeSquared) * cos(phiV);
        float y = sqrt(xyMagnitudeSquared) * sin(phiV);

        // leave z as viewCosine
        float z = viewCosine;

        float3 V = float3(x, y, z);
        V = normalize(V);

        float3 H = sampler.GGXSampleAnisotopic(V, ax, ay);

        float3 L = normalize(reflect(-V, H));

        if (L.z <= 0.0f)
            continue;

        BxDFEval eval = material.EvaluateReflection(V, L, 1.0);

        if (eval.PDF <= 0.0f)
            continue;

        if (isnan(eval.BxDF.x) || isnan(eval.BxDF.y) || isnan(eval.BxDF.z) || isinf(eval.BxDF.x) || isinf(eval.BxDF.y) || isinf(eval.BxDF.z))
            continue;

        finalValue += eval.BxDF.x / eval.PDF; // Since the material.Color = float3(1.0) all channels are the same, and we only care about a single value hence the .x
    }

    uTable[index] += finalValue / push.SampleCount;
}