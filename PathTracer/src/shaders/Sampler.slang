module Sampler;

import rtcommon;

public struct Sampler
{
    uint m_Seed = 0;

    public __init() { m_Seed = 0; }
    public __init(uint seed) { m_Seed = seed; }

    [mutating]
    public uint PCG()
    {
        uint state = m_Seed * 747796405u + 2891336453u;
        uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        m_Seed = (word >> 22u) ^ word;

        return m_Seed;
    }

    [mutating]
    public float UniformFloat()
    {
        uint hash = PCG();

        return float(hash) / float(UINT_MAX);
    }
    
    [mutating]
    public float UniformFloat(float a, float b)
    {
        uint hash = PCG();

        float rand = float(hash) / float(UINT_MAX);
        return (rand * (b - a)) + a;
    }

    [mutating]
    public float3 RandomSphereVec()
    {
        // Spherical Coordinates

        float u1 = UniformFloat();
        float u2 = UniformFloat();

        float theta = 2.0f * M_PI * u1;

        float z = 1.0f - 2.0f * u2;
        float r = sqrt(1.0f - z * z);

        float3 dir;
        dir.x = r * cos(theta);
        dir.y = r * sin(theta);
        dir.z = z;

        return dir;
    }

    [mutating]
    public float3 RandomSphereVecCosineWeight(in float3 normal)
    {
        return normalize(RandomSphereVec() + normal);
    }
}