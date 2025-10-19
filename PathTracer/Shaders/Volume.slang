import RTCommon;
import Sampler;
import Defines;
import Bindings;

#define PNANOVDB_HLSL
#include "PNanoVDB.h"

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

    // This enables faster but biased methods for rendering volumes, especially clouds
    int m_ApproximatedScattering;

    // Helper structures for internal calculations
    struct VolumeTraversalContext
    {
        float3 blockSize;
        float epsilon;
        float tEnter;
        float tExit;
    };

    struct BlockInfo
    {
        int3 indices;
        int blockIndex;
        float3 minCorner;
        float3 maxCorner;
    };

    // Helper function to sample density from NanoVDB grid at world position x
    float SampleNanoVDBBuffer(inout Sampler sampler, StructuredBuffer<uint> gridBuffer, float3 x)
    {
        pnanovdb_grid_handle_t grid = {0}; // Always use the first grid, there's only one

        float minX = (float)pnanovdb_grid_get_world_bbox(gridBuffer, grid, 0);
        float minY = (float)pnanovdb_grid_get_world_bbox(gridBuffer, grid, 1);
        float minZ = (float)pnanovdb_grid_get_world_bbox(gridBuffer, grid, 2);
        float maxX = (float)pnanovdb_grid_get_world_bbox(gridBuffer, grid, 3);
        float maxY = (float)pnanovdb_grid_get_world_bbox(gridBuffer, grid, 4);
        float maxZ = (float)pnanovdb_grid_get_world_bbox(gridBuffer, grid, 5);

        pnanovdb_coord_t worldBboxMin = {(int)floor(minX), (int)floor(minY), (int)floor(minZ)};
        pnanovdb_coord_t worldBboxMax = {(int)ceil(maxX), (int)ceil(maxY), (int)ceil(maxZ)};

        // First normalize position within the volume's bounding box [0,1]
        float3 normalizedPos = (x - m_CornerMin) / (m_CornerMax - m_CornerMin);
        normalizedPos.y = 1.0f - normalizedPos.y; // Flip Y for NanoVDB
        // Then map to NanoVDB grid space
        float3 gridPos = normalizedPos * float3(worldBboxMax - worldBboxMin) + float3(worldBboxMin);

        pnanovdb_uint32_t gridType = pnanovdb_grid_get_grid_type(gridBuffer, grid);
        pnanovdb_tree_handle_t tree = pnanovdb_grid_get_tree(gridBuffer, grid);
        pnanovdb_root_handle_t root = pnanovdb_tree_get_root(gridBuffer, tree);
        pnanovdb_readaccessor_t accessor;
        pnanovdb_readaccessor_init(accessor, root);

        pnanovdb_coord_t bboxMin = pnanovdb_root_get_bbox_min(gridBuffer, root);
        pnanovdb_coord_t bboxMax = pnanovdb_root_get_bbox_max(gridBuffer, root);

        // Convert world coordinates to index coordinates using the grid's transform
        float3 indexSpacePosition = pnanovdb_grid_world_to_indexf(gridBuffer, grid, gridPos);
        pnanovdb_coord_t coord = pnanovdb_hdda_pos_to_ijk(indexSpacePosition);

        // Jitter the coordinate for smoother edges
        coord.x += (sampler.PCG() % 3 - 1);
        coord.y += (sampler.PCG() % 3 - 1);
        coord.z += (sampler.PCG() % 3 - 1);

        // Clamp to valid range, it can't get outside the bounding box
        coord = clamp(coord, bboxMin, bboxMax);

        // Get the address of the value at the coordinate
        pnanovdb_address_t valueAddr = pnanovdb_readaccessor_get_value_address(gridType, gridBuffer, accessor, coord);

        // Read the value
        float value = pnanovdb_read_float(gridBuffer, valueAddr);

        return clamp(value, 0.0f, 1.0f);
    }

    // Helper function to create traversal context
    VolumeTraversalContext CreateTraversalContext(VolumeIntersection intersection)
    {
        VolumeTraversalContext context;
        context.blockSize = (m_CornerMax - m_CornerMin) / float3(MAX_DENSITY_GRID_DIM);
        context.epsilon = 0.0001f * max(m_CornerMax.x - m_CornerMin.x, max(m_CornerMax.y - m_CornerMin.y, m_CornerMax.z - m_CornerMin.z));
        context.tEnter = max(intersection.HitPointNear, 0.0f);
        context.tExit = intersection.HitPointFar;
        return context;
    }

    // Helper function to calculate block information for a given position
    BlockInfo CalculateBlockInfo(float3 position, VolumeTraversalContext context)
    {
        BlockInfo info;
        
        // Calculate block indices
        float3 relativePos = (position - m_CornerMin) / (m_CornerMax - m_CornerMin);
        info.indices = clamp(int3(relativePos * (float)MAX_DENSITY_GRID_DIM), 0, MAX_DENSITY_GRID_DIM - 1);
        
        // Calculate linear index
        info.blockIndex = info.indices.x + info.indices.y * MAX_DENSITY_GRID_DIM + info.indices.z * MAX_DENSITY_GRID_DIM * MAX_DENSITY_GRID_DIM;
        
        // Calculate block bounds
        info.minCorner = m_CornerMin + context.blockSize * float3(info.indices);
        info.maxCorner = info.minCorner + context.blockSize;
        
        return info;
    }

    // Helper function to get effective anisotropy based on depth
    float GetEffectiveAnisotropy(float rayDepth)
    {
        if (m_ApproximatedScattering != 0)
            return pow(max(m_Anisotropy, 0.0f), 1.0f + rayDepth); // decrease anisotropy with depth for faster rendering
        else
            return m_Anisotropy;
    }

    // Helper function to get effective density based on depth
    float GetEffectiveDensity(float baseDensity, float rayDepth)
    {
        if (m_ApproximatedScattering != 0)
            return baseDensity * pow(0.8f, rayDepth); // decrease density with depth for faster rendering
        else
            return baseDensity;
    }

    public float GetDensityAtPoint(inout Sampler sampler, float3 x)
    {
        if (m_DensityDataIndex >= 0)
        {
            // Use NanoVDB for density sampling
            const int resourceIndex = m_DensityDataIndex;
            const StructuredBuffer<uint> densityBuffer = uNanoVDBBuffersDensity[NonUniformResourceIndex(resourceIndex)];

            return SampleNanoVDBBuffer(sampler, densityBuffer, x) * m_Density;
        }
        else
        {
            // Homogeneous volume
            return m_Density;
        }
    }

    public float GetAnisotropy() { return m_Anisotropy; }
    public float GetDensity() { return m_Density; }
    public float3 GetColor() { return m_Color; }
    public float3 GetEmissiveColor() { return m_EmissiveColor; }

    // Shared ray-AABB intersection function
    static VolumeIntersection ComputeRayAABBIntersection(in float3 rayOrigin, in float3 rayDirection, in float3 boxMin, in float3 boxMax)
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

    public VolumeIntersection IntersectWithRay(in float3 rayOrigin, in float3 rayDirection)
    {
        return ComputeRayAABBIntersection(rayOrigin, rayDirection, m_CornerMin, m_CornerMax);
    }

    static public VolumeIntersection IntersectWithRay(in float3 rayOrigin, in float3 rayDirection, in float3 boxMin, in float3 boxMax)
    {
        return ComputeRayAABBIntersection(rayOrigin, rayDirection, boxMin, boxMax);
    }

    public bool IsPointInside(in float3 point)
    {
        return all(point >= m_CornerMin) && all(point <= m_CornerMax);
    }

    public float3 GetEmissionFromTemperatureAtPoint(inout Sampler sampler, float3 x)
    {
        if (!(bool)m_HasTemperatureData)
           return 0.0f;
        
        const int resourceIndex = m_DensityDataIndex;
        const StructuredBuffer<uint> temperatureBuffer = uNanoVDBBuffersDensity[NonUniformResourceIndex(resourceIndex)];
        float temperatureNormalized = SampleNanoVDBBuffer(sampler, temperatureBuffer, x);

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
    public float DoesRayScatterInVolume(in float3 rayOrigin, in float3 rayDirection, inout Sampler sampler, out int tempNullCollisions, in float rayDepth)
    {
        tempNullCollisions = 0;
        VolumeIntersection intersection = IntersectWithRay(rayOrigin, rayDirection);

        if (intersection.HitPointFar < 0.0f)
            return -1.0f; // No intersection

        float distanceInsideVolume = intersection.HitPointFar - max(intersection.HitPointNear, 0.0f);
        if (distanceInsideVolume <= 0.0f)
            return -1.0f; // No valid distance inside volume

        if (m_DensityDataIndex >= 0) // If volume is heterogeneous
        {
            return ProcessHeterogeneousVolumeScattering(rayOrigin, rayDirection, sampler, tempNullCollisions, rayDepth, intersection);
        }
        else // Homogeneous Volume
        {
            // Sample a distance based on the density
            float sampledDistance = sampler.SampleScatteringDistance(m_Density);
            
            if (sampledDistance < distanceInsideVolume)
            {
                return max(intersection.HitPointNear, 0.0f) + sampledDistance; // Return the distance along the ray where scattering occurs
            }
            else
            {
                return -1.0f; // No scattering occurs within the volume
            }
        }
    }

    // Helper function for heterogeneous volume scattering
    float ProcessHeterogeneousVolumeScattering(in float3 rayOrigin, in float3 rayDirection, inout Sampler sampler, out int tempNullCollisions, in float rayDepth, VolumeIntersection intersection)
    {
        VolumeTraversalContext context = CreateTraversalContext(intersection);
        BlockInfo blockInfo = CalculateBlockInfo(rayOrigin + rayDirection * (context.tEnter + context.epsilon), context);
        float t = 0.0f;

        // Max 10000 steps
        for (int i = 0; i < 10000; i++)
        {
            float3 currentPosition = rayOrigin + rayDirection * (context.tEnter + t + context.epsilon);

            VolumeIntersection blockIntersection = Volume::IntersectWithRay(currentPosition, rayDirection, blockInfo.minCorner, blockInfo.maxCorner);

            // Use the precomputed max density for this block
            float maxDensity = GetEffectiveDensity(uVolumeMaxDensities[NonUniformResourceIndex(m_DensityDataIndex)][blockInfo.blockIndex] * m_Density, rayDepth);

            // Sample a distance based on the max density of the block
            float sampledDistance = sampler.SampleScatteringDistance(maxDensity);
            
            // Validate block intersection
            if (blockIntersection.HitPointFar <= 0.0f)
            {
                // Ray doesn't intersect block properly, sometimes caused by precision issues, advance by small amount
                t += context.epsilon;
                if (context.tEnter + t > context.tExit)
                    return -1.0f;
                tempNullCollisions++;
                blockInfo = CalculateBlockInfo(rayOrigin + rayDirection * (context.tEnter + t + context.epsilon), context);
                continue;
            }
            
            float distanceToBlockExit = blockIntersection.HitPointFar - max(blockIntersection.HitPointNear, 0.0f);

            if (sampledDistance > distanceToBlockExit)
            {
                // Move to the next block
                t += distanceToBlockExit + context.epsilon; // Add small epsilon to ensure the ray moves to the next block
                if (context.tEnter + t > context.tExit)
                    return -1.0f; // Exited the volume, no scattering occurred
                tempNullCollisions++;
                blockInfo = CalculateBlockInfo(rayOrigin + rayDirection * (context.tEnter + t + context.epsilon), context);
                continue;
            }

            t += sampledDistance;
            if (context.tEnter + t > context.tExit)
            {
                return -1.0f; // Exited the volume, no scattering occurred
            }

            // Sample the actual density at the scattering position
            float3 scatterPosition = rayOrigin + rayDirection * (context.tEnter + t);
            float densityTextureValue = GetEffectiveDensity(GetDensityAtPoint(sampler, scatterPosition), rayDepth);

            if (densityTextureValue / maxDensity < sampler.UniformFloat())
            {
                // Null Collision, continue sampling
                tempNullCollisions++;
                continue;
            }
            else
            {
                return context.tEnter + t; // Return the distance along the ray where scattering occurs
            }
        }

        // Failed to find a scattering event in the given number of steps, assume no scattering
        return -1.0f;
    }

    public float3 GetScatteringDirection(in float3 incidentDirection, inout Sampler sampler, in float rayDepth)
    {
        #ifdef PHASE_FUNCTION_HENYEY_GREENSTEIN
        {
            float anisotropy = GetEffectiveAnisotropy(rayDepth);
            return sampler.SampleHenyeyGreenstein(incidentDirection, anisotropy);
        }
        #elif defined(PHASE_FUNCTION_DRAINE)
        {
            float anisotropy = GetEffectiveAnisotropy(rayDepth);
            return sampler.SampleDraine(incidentDirection, anisotropy, m_Alpha);
        }
        #elif defined(PHASE_FUNCTION_HENYEY_GREENSTEIN_PLUS_DRAINE)
        {
            return sampler.SampleHGPlusDraine(incidentDirection, m_DropletSize);
        }
        #endif
    }

    public float EvaluatePhaseFunction(in float3 V, in float3 L, in float rayDepth)
    {
        #ifdef PHASE_FUNCTION_HENYEY_GREENSTEIN
        {
            float anisotropy = GetEffectiveAnisotropy(rayDepth);
            return EvaluateHenyeyGreenstein(V, L, anisotropy);
        }
        #elif defined(PHASE_FUNCTION_DRAINE)
        {
            float anisotropy = GetEffectiveAnisotropy(rayDepth);
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

    static public float CalculateVolumesTransmittance(inout Sampler sampler, in float3 origin, in float3 direction, in float rayDepth, out int tempCollisionCount)
    {
        float transmittance = 1.0f;
        tempCollisionCount = 0;
        
        for (uint i = 0; i < uUBO.VolumesCount; i++)
        {
            VolumeIntersection intersection = uVolumes[i].IntersectWithRay(origin, direction);
            intersection.HitPointNear = max(intersection.HitPointNear, 0.0f); // Clamp to 0, ray is inside AABB

            if (uVolumes[i].m_DensityDataIndex >= 0 && intersection.HitPointFar >= 0.0f)
            {
                // Heterogeneous volume, transmittance has to be integrated numerically
                transmittance *= uVolumes[i].ProcessHeterogeneousVolumeTransmittance(sampler, origin, direction, rayDepth, tempCollisionCount, intersection);
                
                if (transmittance <= 0.0f)
                    return 0.0f; // Early termination if transmittance becomes zero
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

    // Helper function for heterogeneous volume transmittance calculation
    float ProcessHeterogeneousVolumeTransmittance(inout Sampler sampler, in float3 origin, in float3 direction, in float rayDepth, inout int tempCollisionCount, VolumeIntersection intersection)
    {
        VolumeTraversalContext context = CreateTraversalContext(intersection);
        BlockInfo blockInfo = CalculateBlockInfo(origin + direction * (context.tEnter + context.epsilon), context);
        float transmittance = 1.0f;
        float t = 0.0f;

        // Max 1000 steps
        int j = 0;
        for (j = 0; j < 1000; j++)
        {
            // Russian roulette based on transmittance
            float p = transmittance;
            if (sampler.UniformFloat() > p)
            {
                // Early termination - ray is absorbed
                return 0.0f;
            }
            transmittance /= p;

            float3 currentPosition = origin + direction * (context.tEnter + t + context.epsilon);

            VolumeIntersection blockIntersection = Volume::IntersectWithRay(currentPosition, direction, blockInfo.minCorner, blockInfo.maxCorner);

            // Use the precomputed max density for this block
            float maxDensity = GetEffectiveDensity(uVolumeMaxDensities[NonUniformResourceIndex(m_DensityDataIndex)][blockInfo.blockIndex] * m_Density, rayDepth);

            // Sample a distance based on the max density of the block
            float sampledDistance = sampler.SampleScatteringDistance(maxDensity);
            
            // Validate block intersection
            if (blockIntersection.HitPointFar <= 0.0f)
            {
                // Ray doesn't intersect block properly, sometimes caused by precision issues, advance by small amount
                t += context.epsilon;
                if (context.tEnter + t > context.tExit)
                    break; // Exited the volume
                tempCollisionCount++;
                blockInfo = CalculateBlockInfo(origin + direction * (context.tEnter + t + context.epsilon), context);
                continue;
            }
            
            float distanceToBlockExit = blockIntersection.HitPointFar - max(blockIntersection.HitPointNear, 0.0f);

            if (sampledDistance > distanceToBlockExit)
            {
                // Move to the next block
                t += distanceToBlockExit + context.epsilon; // Add small epsilon to ensure the ray moves to the next block
                if (context.tEnter + t > context.tExit)
                    break; // Exited the volume
                tempCollisionCount++;
                blockInfo = CalculateBlockInfo(origin + direction * (context.tEnter + t + context.epsilon), context);
                continue;
            }

            t += sampledDistance;
            if (context.tEnter + t > context.tExit)
            {
                break; // Exited the volume
            }

            // Sample the actual density at the interaction position
            float3 interactionPosition = origin + direction * (context.tEnter + t);
            float densityTextureValue = GetEffectiveDensity(GetDensityAtPoint(sampler, interactionPosition), rayDepth);

            // Update transmittance based on actual density vs max density
            transmittance *= 1.0f - (densityTextureValue / maxDensity);
            tempCollisionCount++;
        }

        if (j == 1000)
            return 0.0f;
            
        return transmittance;
    }
};