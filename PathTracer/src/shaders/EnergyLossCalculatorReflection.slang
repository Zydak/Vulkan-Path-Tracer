import Material;
import Sampler;

struct Push
{
    int SampleCount;

    int TableSizeX;
    int TableSizeY;
    int TableSizeZ;
}

[[vk::binding(0, 0)]] RWStructuredBuffer<float> uTable;

[[vk::push_constant]] Push push;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 threadId: SV_DispatchThreadID)
{
    if (threadId.x >= push.TableSizeX || threadId.y >= push.TableSizeY || threadId.z >= push.TableSizeZ)
        return;

    int index = threadId.x + threadId.y * push.TableSizeX + threadId.z * push.TableSizeX * push.TableSizeY;

    Sampler sampler = Sampler(threadId.y + threadId.x * threadId.x + getRealtimeClock().x);

    // Set all values
    float viewCosine = clamp(float(threadId.x) / push.TableSizeX, 0.0001, 0.9999);
    float roughness = clamp(float(threadId.y) / push.TableSizeY, 0.0001, 1.0);
    float anisotropy = float(threadId.z) / push.TableSizeZ;
    const float aspect = sqrt(1.0 - sqrt(anisotropy) * 0.9);
    float ax = max(0.001, roughness / aspect);
    float ay = max(0.001, roughness * aspect);

    // Prepare material
    Material material;
    material.Anisotropy = anisotropy;
    material.Roughness = roughness;
    material.Color = float4(1.0, 1.0, 1.0, 1.0);
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

        if (L.z < 0.0)
            continue;

        float PDF;
        float3 BSDF = material.EvaluateReflection(V, L, 1.0, PDF);

        if (PDF <= 0.0f)
            continue;

        if (isnan(L.x) || isnan(L.y) || isnan(L.z) || isinf(L.x) || isinf(L.y) || isinf(L.z))
            continue;

        if (isnan(BSDF.x) || isnan(BSDF.y) || isnan(BSDF.z) || isinf(BSDF.x) || isinf(BSDF.y) || isinf(BSDF.z))
            continue;

        finalValue += BSDF.x / PDF; // Since the material.Color = float3(1.0) all channels are the same, and we only care about a single value hence the .x
    }

    uTable[index] += finalValue / push.SampleCount;
}