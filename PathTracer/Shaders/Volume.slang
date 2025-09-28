import RTCommon;
import Sampler;
import Defines;
import Bindings;

[[vk::binding(13, 0)]] public StructuredBuffer<Volume, ScalarDataLayout> uVolumes;

#define MAX_DENSITY_GRID_DIM 32

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
    float3 m_TemperatureColor;
    float m_Density;
    float m_Anisotropy;
    float m_Alpha;
    float m_DropletSize;

    int m_DensityDataIndex; // -1 if homogeneous

    // I have no clue why I can't use bool here, the layout ends up incorrect even tho I use ScalarDataLayout
    int m_UseBlackbody; // 1 to use blackbody radiation, 0 to use m_TemperatureColor
    int m_HasTemperatureData; // 1 if temperature data is available, 0 otherwise
    float m_TemperatureGamma; // Exponent for temperature to compute blackbody radiation
    float m_TemperatureScale; // Scales the temperature value read from the grid before using it to compute emission
    float m_EmissiveColorGamma; // Exponent for emissive color
    int m_KelvinMin;
    int m_KelvinMax;

    // This enables faster but biased methods for rendering clouds
    int m_ApproximatedScatteringForClouds;

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

    static public VolumeIntersection IntersectWithRay(in float3 rayOrigin, in float3 rayDirection, in float3 boxMin, in float3 boxMax)
    {
        float3 invDir = 1.0f / rayDirection;
        float3 t0s = (boxMin - rayOrigin) * invDir;
        float3 t1s = (boxMax - rayOrigin) * invDir;

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

    public float3 GetEmissionFromTemperatureAtPoint(float3 x)
    {
        if (!(bool)m_HasTemperatureData)
           return 0.0f;

        uint3 textureSize;
        uint numberOfLevels;
        uVolumeTemperatureTexture[NonUniformResourceIndex(m_DensityDataIndex)].GetDimensions(0, textureSize.x, textureSize.y, textureSize.z, numberOfLevels);

        float3 UV = (x - m_CornerMin) / (m_CornerMax - m_CornerMin);
        UV.z *= textureSize.z; // Scale Z to sample the texture array
        float temperatureNormalized = uVolumeTemperatureTexture[NonUniformResourceIndex(m_DensityDataIndex)].SampleLevel(uTextureSampler, UV, 0).r;

        float3 color;

        if ((bool)m_UseBlackbody)
        {
            // Map normalized temperature to Kelvin range
            float temperatureKelvins = temperatureNormalized * (m_KelvinMax - m_KelvinMin) + m_KelvinMin;
            color = Blackbody(temperatureKelvins);
        }
        else
            color = m_TemperatureColor;

        float intensity = pow(temperatureNormalized, m_TemperatureGamma) * m_TemperatureScale;

        return intensity * pow(color, float3(m_EmissiveColorGamma));
    }

    // Returns distance from rayOrigin at which scattering occured inside the volume, or -1 if no scattering occurred
    public float DoesRayScatterInVolume(in float3 rayOrigin, in float3 rayDirection, inout Sampler sampler, out int tempNullCollisions, in float tempDepth)
    {
        tempNullCollisions = 0;
        VolumeIntersection intersection = IntersectWithRay(rayOrigin, rayDirection);

        if (intersection.HitPointFar < 0.0f)
            return -1.0f; // No intersection

        float tEnter = max(intersection.HitPointNear, 0.0f); // Clamp to 0, ray is inside AABB
        float tExit = intersection.HitPointFar;

        float distanceInsideVolume = tExit - tEnter;
        if (distanceInsideVolume <= 0.0f)
            return -1.0f; // No valid distance inside volume

        if (m_DensityDataIndex >= 0) // If volume is heterogeneous
        {
            uint3 textureSize;
            uint numberOfLevels;
            uVolumeDensityTexture[NonUniformResourceIndex(m_DensityDataIndex)].GetDimensions(0, textureSize.x, textureSize.y, textureSize.z, numberOfLevels);

            float3 blockSize = (m_CornerMax - m_CornerMin) / float3(MAX_DENSITY_GRID_DIM); // 32x32x32 blocks
            float t = 0.0f;

            float epsilon = 0.0001f * max(m_CornerMax.x - m_CornerMin.x, max(m_CornerMax.y - m_CornerMin.y, m_CornerMax.z - m_CornerMin.z)); // Small epsilon based on scene scale

            // Max 100 steps
            for (int i = 0; i < 10000; i++)
            {
                float3 currentPosition = rayOrigin + rayDirection * (tEnter + t);

                // Calculate current block indices (ensure they're in valid range [0,15])
                int3 block3DIndex;
                float3 relativePos = (currentPosition - m_CornerMin) / (m_CornerMax - m_CornerMin);
                block3DIndex.x = max(0, min(MAX_DENSITY_GRID_DIM - 1, int(relativePos.x * MAX_DENSITY_GRID_DIM)));
                block3DIndex.y = max(0, min(MAX_DENSITY_GRID_DIM - 1, int(relativePos.y * MAX_DENSITY_GRID_DIM)));
                block3DIndex.z = max(0, min(MAX_DENSITY_GRID_DIM - 1, int(relativePos.z * MAX_DENSITY_GRID_DIM)));

                int blockIndex = block3DIndex.x + block3DIndex.y * MAX_DENSITY_GRID_DIM + block3DIndex.z * MAX_DENSITY_GRID_DIM * MAX_DENSITY_GRID_DIM;

                float3 blockMin = m_CornerMin + blockSize * float3(block3DIndex);
                float3 blockMax = blockMin + blockSize;

                VolumeIntersection blockIntersection = Volume::IntersectWithRay(currentPosition, rayDirection, blockMin, blockMax);

                // Use the precomputed max density for this block
                float maxDensity = uVolumeMaxDensities[NonUniformResourceIndex(m_DensityDataIndex)][blockIndex] * m_Density;

                if (m_ApproximatedScatteringForClouds != 0)
                    maxDensity *= pow(0.8f, tempDepth);

                // Sample a distance based on the max density of the block
                float sampledDistance = sampler.SampleScatteringDistance(maxDensity);
                
                // Validate block intersection
                if (blockIntersection.HitPointFar <= 0.0f)
                {
                    // Ray doesn't intersect block properly, sometimes caused by precision issues I guess, advance by small amount
                    t += epsilon;
                    if (tEnter + t > tExit)
                        return -1.0f;
                    tempNullCollisions++;
                    continue;
                }
                
                float distanceToBlockExit = blockIntersection.HitPointFar - max(blockIntersection.HitPointNear, 0.0f);

                if (sampledDistance > distanceToBlockExit)
                {
                    // Move to the next block
                    t += distanceToBlockExit + epsilon; // Add small epsilon to ensure the ray moves to the next block
                    if (tEnter + t > tExit)
                        return -1.0f; // Exited the volume, no scattering occurred
                    tempNullCollisions++;
                    continue;
                }

                t += sampledDistance;
                if (tEnter + t > tExit)
                {
                    return -1.0f; // Exited the volume, no scattering occurred
                }

                // Sample the actual density at the scattering position
                float3 scatterPosition = rayOrigin + rayDirection * (tEnter + t);
                float3 UV = (scatterPosition - m_CornerMin) / (m_CornerMax - m_CornerMin);
                UV.z *= textureSize.z; // Scale Z to sample the texture array
                float densityTextureValue = clamp(uVolumeDensityTexture[NonUniformResourceIndex(m_DensityDataIndex)].SampleLevel(uTextureSampler, UV, 0).r, 0.0f, 1.0f) * m_Density;

                if (m_ApproximatedScatteringForClouds != 0)
                    densityTextureValue *= pow(0.8f, tempDepth);

                if (densityTextureValue / maxDensity < sampler.UniformFloat())
                {
                    // Null Collision, continue sampling
                    tempNullCollisions++;
                    continue;
                }
                else
                {
                    return tEnter + t; // Return the distance along the ray where scattering occurs
                }
            }

            // Failed to find a scattering event in the given number of steps, assume no scattering
            return -1.0f;
        }
        else // Homogeneous Volume
        {
            // Sample a distance based on the density
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
    }

    public float3 GetScatteringDirection(in float3 incidentDirection, inout Sampler sampler, in float tempDepth)
    {
        float3 newdir;
        #ifdef PHASE_FUNCTION_HENYEY_GREENSTEIN
        {
            float anisotropy;
            if (m_ApproximatedScatteringForClouds != 0)
                anisotropy = pow(m_Anisotropy, 1.0f + tempDepth);
            else
                anisotropy = m_Anisotropy;

            newdir = sampler.SampleHenyeyGreenstein(incidentDirection, anisotropy);
        }
        #elif defined(PHASE_FUNCTION_DRAINE)
        {
            float anisotropy;
            if (m_ApproximatedScatteringForClouds != 0)
                anisotropy = pow(m_Anisotropy, 1.0f + tempDepth);
            else
                anisotropy = m_Anisotropy;

            newdir = sampler.SampleDraine(incidentDirection, anisotropy, m_Alpha);
        }
        #elif defined(PHASE_FUNCTION_HENYEY_GREENSTEIN_PLUS_DRAINE)
        {
            newdir = sampler.SampleHGPlusDraine(incidentDirection, m_DropletSize);
        }
        #endif

        return newdir;
    }

    public float EvaluatePhaseFunction(in float3 V, in float3 L, in float tempDepth)
    {
        #ifdef PHASE_FUNCTION_HENYEY_GREENSTEIN
        {
            float anisotropy;
            if (m_ApproximatedScatteringForClouds != 0)
                anisotropy = pow(m_Anisotropy, 1.0f + tempDepth);
            else
                anisotropy = m_Anisotropy;

            return EvaluateHenyeyGreenstein(V, L, anisotropy);
        }
        #elif defined(PHASE_FUNCTION_DRAINE)
        {
            float anisotropy;
            if (m_ApproximatedScatteringForClouds != 0)
                anisotropy = pow(m_Anisotropy, 1.0f + tempDepth);
            else
                anisotropy = m_Anisotropy;

            return EvaluateDraine(V, L, anisotropy, m_Alpha);
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
        return PhaseHenyeyGreenstein(V, L, g);
    }

    public float EvaluateDraine(float3 V, float3 L, in float g, in float a)
    {
        return PhaseDraine(V, L, g, a);
    }

    static public float CalculateVolumesTransmittance(inout Sampler sampler, in float3 origin, in float3 direction, in float tempDepth)
    {
        float transmittance = 1.0f;
        for (uint i = 0; i < uUBO.VolumesCount; i++)
        {
            VolumeIntersection intersection = uVolumes[i].IntersectWithRay(origin, direction);
            intersection.HitPointNear = max(intersection.HitPointNear, 0.0f); // Clamp to 0, ray is inside AABB

            if (uVolumes[i].m_DensityDataIndex >= 0 && intersection.HitPointFar >= 0.0f)
            {
                // Heterogeneous volume, transmittance has to be integrated numerically
                
                uint3 textureSize;
                uint numberOfLevels;
                uVolumeDensityTexture[NonUniformResourceIndex(uVolumes[i].m_DensityDataIndex)].GetDimensions(0, textureSize.x, textureSize.y, textureSize.z, numberOfLevels);

                float3 blockSize = (uVolumes[i].m_CornerMax - uVolumes[i].m_CornerMin) / float3(MAX_DENSITY_GRID_DIM); // 32x32x32 blocks
                float t = 0.0f;
                float tEnter = intersection.HitPointNear;
                float tExit = intersection.HitPointFar;

                float epsilon = 0.0001f * max(uVolumes[i].m_CornerMax.x - uVolumes[i].m_CornerMin.x, max(uVolumes[i].m_CornerMax.y - uVolumes[i].m_CornerMin.y, uVolumes[i].m_CornerMax.z - uVolumes[i].m_CornerMin.z)); // Small epsilon based on scene scale

                // Max 100K steps
                for (int j = 0; j < 10000; j++)
                {
                    float3 currentPosition = origin + direction * (tEnter + t);

                    // Calculate current block indices (ensure they're in valid range [0,15])
                    int3 block3DIndex;
                    float3 relativePos = (currentPosition - uVolumes[i].m_CornerMin) / (uVolumes[i].m_CornerMax - uVolumes[i].m_CornerMin);
                    block3DIndex.x = max(0, min(MAX_DENSITY_GRID_DIM - 1, int(relativePos.x * MAX_DENSITY_GRID_DIM)));
                    block3DIndex.y = max(0, min(MAX_DENSITY_GRID_DIM - 1, int(relativePos.y * MAX_DENSITY_GRID_DIM)));
                    block3DIndex.z = max(0, min(MAX_DENSITY_GRID_DIM - 1, int(relativePos.z * MAX_DENSITY_GRID_DIM)));

                    int blockIndex = block3DIndex.x + block3DIndex.y * MAX_DENSITY_GRID_DIM + block3DIndex.z * MAX_DENSITY_GRID_DIM * MAX_DENSITY_GRID_DIM;

                    float3 blockMin = uVolumes[i].m_CornerMin + blockSize * float3(block3DIndex);
                    float3 blockMax = blockMin + blockSize;

                    VolumeIntersection blockIntersection = Volume::IntersectWithRay(currentPosition, direction, blockMin, blockMax);

                    // Use the precomputed max density for this block
                    float maxDensity = uVolumeMaxDensities[NonUniformResourceIndex(uVolumes[i].m_DensityDataIndex)][blockIndex] * uVolumes[i].m_Density;

                    if (uVolumes[i].m_ApproximatedScatteringForClouds != 0)
                        maxDensity *= pow(0.8f, tempDepth);

                    // Sample a distance based on the max density of the block
                    float sampledDistance = sampler.SampleScatteringDistance(maxDensity);
                    
                    // Validate block intersection
                    if (blockIntersection.HitPointFar <= 0.0f)
                    {
                        // Ray doesn't intersect block properly, sometime caused by precision issues I guess, advance by small amount
                        t += epsilon;
                        if (tEnter + t > tExit)
                            break; // Exited the volume
                        continue;
                    }
                    
                    float distanceToBlockExit = blockIntersection.HitPointFar - max(blockIntersection.HitPointNear, 0.0f);

                    if (sampledDistance > distanceToBlockExit)
                    {
                        // Move to the next block
                        t += distanceToBlockExit + epsilon; // Add small epsilon to ensure the ray moves to the next block
                        if (tEnter + t > tExit)
                            break; // Exited the volume
                        continue;
                    }

                    t += sampledDistance;
                    if (tEnter + t > tExit)
                    {
                        break; // Exited the volume
                    }

                    // Sample the actual density at the interaction position
                    float3 interactionPosition = origin + direction * (tEnter + t);
                    float3 UV = (interactionPosition - uVolumes[i].m_CornerMin) / (uVolumes[i].m_CornerMax - uVolumes[i].m_CornerMin);
                    UV.z *= textureSize.z; // Scale Z to sample the texture array
                    float densityTextureValue = clamp(uVolumeDensityTexture[NonUniformResourceIndex(uVolumes[i].m_DensityDataIndex)].SampleLevel(uTextureSampler, UV, 0).r, 0.0f, 1.0f) * uVolumes[i].GetDensity();

                    if (uVolumes[i].m_ApproximatedScatteringForClouds != 0)
                        densityTextureValue *= pow(0.8f, tempDepth);

                    // Update transmittance based on actual density vs max density
                    transmittance *= 1.0f - (densityTextureValue / maxDensity);

                    // Russian roulette based on transmittance
                    float p = transmittance;
                    if (sampler.UniformFloat() > p)
                    {
                        // Early termination - ray is absorbed
                        return 0.0f;
                    }
                    transmittance /= p;
                }
            }
            else
            {
                // Homogeneous volume, transmittance can be calculated analytically
                float pathThroughVolumeLength = intersection.HitPointFar - intersection.HitPointNear;
                if (pathThroughVolumeLength > 0.0f)
                    transmittance *= exp(-uVolumes[i].GetDensity() * pathThroughVolumeLength);
            }
        }

        return clamp(transmittance, 0.0f, 1.0f);
    }
};