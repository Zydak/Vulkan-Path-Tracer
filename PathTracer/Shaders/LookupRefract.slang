//
// Reference [https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf]
//

import Material;
import Sampler;
import Defines;

struct Push
{
    uint Seed;
    uint SampleCount;

    uint TableSizeX;
    uint TableSizeY;
    uint TableSizeZ;
}

[[vk::binding(0, 0)]] RWStructuredBuffer<float> uTable;

[[vk::push_constant]] Push push;

[shader("compute")]
[numthreads(8, 8, 1)]
void Main(uint3 threadId: SV_DispatchThreadID)
{
    if (threadId.x >= push.TableSizeX || threadId.y >= push.TableSizeY || threadId.z >= push.TableSizeZ)
        return;

    int index = threadId.x + threadId.y * push.TableSizeX + threadId.z * push.TableSizeX * push.TableSizeY;

    Sampler sampler = Sampler(threadId.y + threadId.x * threadId.x + push.Seed);

    // Set all values
    float viewCosine = clamp(pow(float(threadId.x) / (push.TableSizeX - 1.0f), 2.0), 0.0001, 0.9999); // Stored as ViewCosine^2 for more precision near 0
    float roughness = clamp(float(threadId.y) / (push.TableSizeY - 1.0f), 0.0001, 1.0);
    float ior = 1.0f + clamp(float(threadId.z) / (push.TableSizeZ - 1.0f), 0.0001, 1.0);

    // Prepare material
    Material material;
    material.Properties.Roughness = roughness;
    material.Properties.BaseColor = float3(1.0, 1.0, 1.0);
    material.Properties.IOR = ior;
    material.Ax = roughness;
    material.Ay = roughness;
#ifdef ABOVE_SURFACE
    material.Eta = (1.0f / ior);
#else
    material.Eta = ior;
#endif

    float finalValue = 0.0;
    for (int i = 0; i < push.SampleCount; i++)
    {
        // Generate random view dir
        float xyMagnitude = sqrt(1.0 - viewCosine * viewCosine);
        float phiV = sampler.UniformFloat() * M_2_PI;
        float x = xyMagnitude * cos(phiV);
        float y = xyMagnitude * sin(phiV);

        // leave z as viewCosine
        float z = viewCosine;

        float3 V = float3(x, y, z);
        V = normalize(V);

        float3 H = sampler.GGXSampleAnisotopic(V, material.Ax, material.Ay);

        float F = material.DielectricFresnel(abs(dot(V, H)));

        float val = 0.0;
        if (sampler.UniformFloat() < F)
        {
            // Reflection
            float3 L = normalize(reflect(-V, H));

            if (L.z > 0.0)
            {
                BxDFEval eval = material.EvaluateReflection(V, L, 1.0f);

                if (eval.PDF > 0.0 && !isnan(eval.BxDF.x) && !isinf(eval.BxDF.x))
                    val += eval.BxDF.x / eval.PDF;
            }
        }
        else
        {
            // Refraction
            float3 L = normalize(refract(-V, H, material.Eta));

            if (L.z < 0.0)
            {
                BxDFEval eval = material.EvaluateRefraction(V, L, 0.0f);

                if (eval.PDF > 0.0 && !isnan(eval.BxDF.x) && !isinf(eval.BxDF.x))
                    val += eval.BxDF.x / eval.PDF;
            }
        }

        if (!isnan(val) && !isinf(val))
            finalValue += val;
    }

    uTable[index] += finalValue / push.SampleCount;
}