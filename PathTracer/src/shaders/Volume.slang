import rtcommon;
import Sampler;

public struct AABB
{
    public float4 Min;
    public float4 Max;

    public void IntersectRay(in float3 rayOrigin, in float3 rayDirection, out float3 hitPointNear, out float3 hitPointFar)
    {
        float3 tMin = (Min.xyz - rayOrigin) / rayDirection;
        float3 tMax = (Max.xyz - rayOrigin) / rayDirection;

        float3 t1 = min(tMin, tMax);
        float3 t2 = max(tMin, tMax);

        float tNear = max(max(t1.x, t1.y), t1.z);
        float tFar = min(min(t2.x, t2.y), t2.z);

        if (tNear > tFar || tFar < 0.0f) {
            // No intersection
            hitPointNear = float3(FLT_MAX);
            hitPointFar = float3(FLT_MAX);
            return;
        }

        // Ray intersects AABB
        hitPointNear = rayOrigin + rayDirection * tNear;
        hitPointFar = rayOrigin + rayDirection * tFar;
    }

    public bool IsInsideAABB(float3 point)
    {
        if (
            (Min.x <= point.x && Max.x >= point.x) &&
            (Min.y <= point.y && Max.y >= point.y) &&
            (Min.z <= point.z && Max.z >= point.z)
        )
        {
            return true;
        }
        return false;
    }
};

public struct Volume
{
    public AABB Aabb;
    public float4 Color;
    public float ScatteringCoefficient;
    public float G;

    public float SampleScatterDistance(inout Sampler sampler)
    {
        return -log(sampler.UniformFloat()) / ScatteringCoefficient;
    }

    public float EvaluateHenyeyGreenstein(float3 V, float3 L)
    {
        // return 1.0f / (2.0f);
        return (1.0f / (4.0f * M_PI)) * ((1.0f - G * G) / pow(1.0f + G * G - 2.0f * G * dot(V, L), 1.5f));
    }
};