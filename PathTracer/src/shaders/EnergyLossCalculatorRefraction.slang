import Material;
import Sampler;

struct Push
{
    int SampleCount;

    int TableSizeX;
    int TableSizeY;
    int TableSizeZ;

    bool AboveSurface;
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
    float viewCosine = clamp(pow(float(threadId.x) / (push.TableSizeX - 1.0f), 4.0), 0.0001, 0.9999);
    float roughness = clamp(float(threadId.y) / (push.TableSizeY - 1.0f), 0.0001, 1.0);
    float ior = 1.0f + clamp(pow(float(threadId.z) / (push.TableSizeZ - 1.0f), 4.0), 0.0001, 1.0) * 2.0f;
    float ax = roughness;
    float ay = roughness;

    // Prepare material
    Material material;
    material.Roughness = roughness;
    material.Color = float4(1.0, 1.0, 1.0, 1.0);
    material.Ax = ax;
    material.Ay = ay;
    material.Ior = ior;
    material.Eta = push.AboveSurface ? ior : 1.0f / ior;

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

        float F = material.DielectricFresnel(abs(dot(V, H)), material.Eta);

        float valRefract = 0.0;
        float valReflect = 0.0;
        if (sampler.UniformFloat() < F)
        {
            for (int j = 0; j < 1; j++)
            {
                // Reflection
                float3 L = normalize(reflect(-V, H));

                if (L.z <= 0.0)
                    continue;

                float PDF;
                float3 BSDF = material.EvaluateReflection(V, L, F, PDF);
                PDF *= F;

                valReflect += BSDF.x / PDF;
            }
        }
        else
        {
            for (int j = 0; j < 1; j++)
            {
                // Refraction
                float3 L = normalize(refract(-V, H, material.Eta));
        
                if (L.z >= 0.0)
                    continue;
        
                float PDF;
                float3 BSDF = material.EvaluateRefraction(V, L, F, PDF);
                PDF *= 1.0f - F;

                valRefract += BSDF.x / PDF;
            }
        }

        if (!isnan(valRefract) && !isinf(valRefract))
            finalValue += valRefract;

        if (!isnan(valReflect) && !isinf(valReflect))
            finalValue += valReflect;

    }

    uTable[index] += finalValue / push.SampleCount;
}