import RTCommon;
import Sampler;
import Defines;

public struct VolumeIntersection
{
    public float HitPointNear;
    public float HitPointFar;
}

public struct Volume
{
    // AABB
    float3 m_CornerMin;
    float3 m_CornerMax;

    float3 m_Color;
    float3 m_EmissiveColor;
    float m_Density;
    float m_Anisotropy; // Scattering anisotropy (-1 to 1)

    public float GetAnisotropy() { return m_Anisotropy; }
    public float GetDensity() { return m_Density; }
    public float3 GetColor() { return m_Color; }
    public float3 GetEmissiveColor() { return m_EmissiveColor; }

    public VolumeIntersection IntersectWithRay(in float3 rayOrigin, in float3 rayDirection)
    {
        float3 invDir = 1.0f / rayDirection;
        float3 t0s = (m_CornerMin - rayOrigin) * invDir;
        float3 t1s = (m_CornerMax - rayOrigin) * invDir;

        float3 tsmaller = min(t0s, t1s);
        float3 tbigger = max(t0s, t1s);

        float tmin = max(max(tsmaller.x, tsmaller.y), max(tsmaller.x, tsmaller.z));
        float tmax = min(min(tbigger.x, tbigger.y), min(tbigger.x, tbigger.z));

        VolumeIntersection result;
        result.HitPointNear = tmin;
        result.HitPointFar = tmax;

        if (tmax < 0.0f || tmin > tmax)
        {
            result.HitPointNear = -1.0f;
            result.HitPointFar = -1.0f;
        }

        return result;
    }

    public bool IsPointInside(in float3 point)
    {
        return all(point >= m_CornerMin) && all(point <= m_CornerMax);
    }

    public float DoesRayScatterInVolume(in float3 rayOrigin, in float3 rayDirection, inout Sampler sampler)
    {
        VolumeIntersection intersection = IntersectWithRay(rayOrigin, rayDirection);
        if (IsPointInside(rayOrigin))
        {
            intersection.HitPointNear = 0.0f; // Start inside the volume
        }

        if (intersection.HitPointNear < 0.0f)
            return -1.0f; // No intersection

        float tEnter = max(intersection.HitPointNear, 0.0f); // Clamp to 0 to avoid going backwards
        float tExit = intersection.HitPointFar;

        float distanceInsideVolume = tExit - tEnter;
        if (distanceInsideVolume <= 0.0f)
            return -1.0f; // No valid distance inside volume

        // Sample a distance based on the density
        float randomSample = sampler.UniformFloat();
        float sampledDistance = sampler.SampleScatteringDistance(m_Density);

        if (sampledDistance < distanceInsideVolume)
        {
            return tEnter + sampledDistance; // Return the distance along the ray where scattering occurs
        }
        else
        {
            return -1.0f; // No scattering occurs within the volume
        }
    }

    public float EvaluateHenyeyGreenstein(float3 V, float3 L)
    {
        return (1.0f / (4.0f * M_PI)) * ((1.0f - m_Anisotropy * m_Anisotropy) / pow(1.0f + m_Anisotropy * m_Anisotropy - 2.0f * m_Anisotropy * dot(V, L), 1.5f));
    }
};