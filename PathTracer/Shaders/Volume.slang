import RTCommon;
import Sampler;
import Defines;

[[vk::image_format("r32f")]]
[[vk::binding(19, 0)]] Texture2DArray uVolumeDensityTexture[];

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
    float m_Anisotropy;
    float m_Alpha;
    float m_DropletSize;

    int m_HeterogeneousTextureIndex; // -1 if homogeneous
    float m_HeterogenousSmoothing;

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

    // Returns distance from rayOrigin at which scattering occured inside the volume, or -1 if no scattering occurred
    public float DoesRayScatterInVolume(in float3 rayOrigin, in float3 rayDirection, inout Sampler sampler, in SamplerState uTextureSampler, out bool isNullCollision)
    {
        isNullCollision = false;

        VolumeIntersection intersection = IntersectWithRay(rayOrigin, rayDirection);

        if (intersection.HitPointFar < 0.0f)
            return -1.0f; // No intersection

        float tEnter = max(intersection.HitPointNear, 0.0f); // Clamp to 0, ray is inside AABB
        float tExit = intersection.HitPointFar;

        float distanceInsideVolume = tExit - tEnter;
        if (distanceInsideVolume <= 0.0f)
            return -1.0f; // No valid distance inside volume

        // Sample a distance based on the density
        float randomSample = sampler.UniformFloat();
        float sampledDistance = sampler.SampleScatteringDistance(m_Density);

        // Heterogeneous volumes use delta tracking to determine if a null collision occurs
        if (m_HeterogeneousTextureIndex >= 0)
        {
            // Find UV of the texture, 3D position inside the volume where 0 is m_CornerMin and 1 is m_CornerMax
            float3 positionInVolume = rayOrigin + rayDirection * (tEnter + sampledDistance);
            float3 UV = (positionInVolume - m_CornerMin) / (m_CornerMax - m_CornerMin);

            uint3 textureSize;
            uint numberOfLevels;
            uVolumeDensityTexture[NonUniformResourceIndex(m_HeterogeneousTextureIndex)].GetDimensions(0, textureSize.x, textureSize.y, textureSize.z, numberOfLevels);
            UV.z *= textureSize.z; // Scale Z to fit in the texture array

            float densityTextureValue = clamp(uVolumeDensityTexture[NonUniformResourceIndex(m_HeterogeneousTextureIndex)].SampleLevel(uTextureSampler, UV, 0).r * m_HeterogenousSmoothing, 0.0f, 1.0f) * m_Density;

            if (densityTextureValue / m_Density < sampler.UniformFloat())
            {
                // Null Collision
                isNullCollision = true;
            }
        }

        if (sampledDistance < distanceInsideVolume)
        {
            return tEnter + sampledDistance; // Return the distance along the ray where scattering occurs
        }
        else
        {
            return -1.0f; // No scattering occurs within the volume
        }
    }

    public float3 GetScatteringDirection(in float3 incidentDirection, inout Sampler sampler)
    {
        float3 newdir;
        #ifdef PHASE_FUNCTION_HENYEY_GREENSTEIN
        {
            newdir = sampler.SampleHenyeyGreenstein(incidentDirection, m_Anisotropy);
        }
        #elif defined(PHASE_FUNCTION_DRAINE)
        {
            newdir = sampler.SampleDraine(incidentDirection, m_Anisotropy, m_Alpha);
        }
        #elif defined(PHASE_FUNCTION_HENYEY_GREENSTEIN_PLUS_DRAINE)
        {
            newdir = sampler.SampleHGPlusDraine(incidentDirection, m_DropletSize);
        }
        #endif

        return newdir;
    }

    public float EvaluatePhaseFunction(in float3 V, in float3 L)
    {
        #ifdef PHASE_FUNCTION_HENYEY_GREENSTEIN
        {
            return EvaluateHenyeyGreenstein(V, L, m_Anisotropy);
        }
        #elif defined(PHASE_FUNCTION_DRAINE)
        {
            return EvaluateDraine(V, L, m_Anisotropy, m_Alpha);
        }
        #elif defined(PHASE_FUNCTION_HENYEY_GREENSTEIN_PLUS_DRAINE)
        {
            return EvaluateHGPlusDraine(V, L);
        }
        #endif
    }

    public float EvaluateHGPlusDraine(float3 V, float3 L)
    {
        const float d = m_DropletSize;
        const float GHG = exp(-(0.0990567f / (d - 1.67154)));
        const float GD = exp(-(2.20679f / (d + 3.91029)) - 0.428934);
        const float ALPHA_D = exp(3.62489f - (8.29288f / (d + 5.52825)));
        const float W_D = exp(-(0.599085f / (d - 0.641583)) - 0.665888);

        float HG = EvaluateHenyeyGreenstein(V, L, GHG);
        float D = EvaluateDraine(V, L, GD, ALPHA_D);
        return lerp(HG, D, W_D);
    }

    public float EvaluateHenyeyGreenstein(float3 V, float3 L, in float g)
    {
        if (g == 0.0f)
            return 1.0f / (4.0f * M_PI);

        return (1.0f / (4.0f * M_PI)) * ((1.0f - g * g) / pow(1.0f + g * g - 2.0f * g * dot(V, L), 1.5f));
    }

    public float EvaluateDraine(float3 V, float3 L, in float g, in float a)
    {
        float u = dot(V, L);
        
        return ((1 - g*g)*(1 + a*u*u))/(4.*(1 + (a*(1 + 2*g*g))/3.) * M_PI * pow(1 + g*g - 2*g*u,1.5));
    }

    static public float CalculateVolumesTransmittance(inout Sampler sampler, in float3 origin, in float3 direction, in StructuredBuffer<Volume, ScalarDataLayout> uVolumes, in uint volumesCount, in SamplerState uTextureSampler)
    {
        float transmittance = 1.0f;
        for (uint i = 0; i < volumesCount; i++)
        {
            VolumeIntersection intersection = uVolumes[i].IntersectWithRay(origin, direction);
            intersection.HitPointNear = max(intersection.HitPointNear, 0.0f); // Clamp to 0, ray is inside AABB

            if (uVolumes[i].m_HeterogeneousTextureIndex >= 0 && intersection.HitPointFar >= 0.0f)
            {
                // Heterogeneous volume, transmittance has to be integrated numerically

                float t = 0.0f;
                // Max 200 steps
                for (int j = 0; j < 500; j++)
                {
                    t += sampler.SampleScatteringDistance(uVolumes[i].GetDensity());

                    if (intersection.HitPointNear + t > intersection.HitPointFar)
                    {
                        // Exited the volume
                        return transmittance;
                    }

                    // Find UV of the texture, 3D position inside the volume where 0 is m_CornerMin and 1 is m_CornerMax
                    float3 positionInVolume = origin + direction * (intersection.HitPointNear + t);
                    float3 UV = (positionInVolume - uVolumes[i].m_CornerMin) / (uVolumes[i].m_CornerMax - uVolumes[i].m_CornerMin);

                    uint3 textureSize;
                    uint numberOfLevels;
                    uVolumeDensityTexture[NonUniformResourceIndex(uVolumes[i].m_HeterogeneousTextureIndex)].GetDimensions(0, textureSize.x, textureSize.y, textureSize.z, numberOfLevels);
                    UV.z *= textureSize.z; // Scale Z to fit in the texture array

                    float densityTextureValue = clamp(uVolumeDensityTexture[NonUniformResourceIndex(uVolumes[i].m_HeterogeneousTextureIndex)].SampleLevel(uTextureSampler, UV, 0).r * uVolumes[i].m_HeterogenousSmoothing, 0.0f, 1.0f) * uVolumes[i].GetDensity();
                    transmittance *= 1.0f - (densityTextureValue / uVolumes[i].GetDensity());

                    // Russian roulette based on transmittance
                    float p = transmittance;
                    if (sampler.UniformFloat() > p)
                    {
                        // Early termination - ray is absorbed
                        return 0.0f;
                    }
                    transmittance /= p;
                }

                // Failed to exit the volume in the given number of steps, assume fully absorbed
                return 0.0f;
            }
            else
            {
                // Homogeneous volume, transmittance can be calculated analytically
                float pathThroughVolumeLength = intersection.HitPointFar - intersection.HitPointNear;
                if (pathThroughVolumeLength > 0.0f)
                    transmittance *= exp(-uVolumes[i].GetDensity() * pathThroughVolumeLength);
            }
        }

        return transmittance;
    }
};