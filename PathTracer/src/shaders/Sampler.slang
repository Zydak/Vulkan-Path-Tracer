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
    public float2 UniformFloat2()
    {
        uint hash = PCG();
        float x1 = float(hash) / float(UINT_MAX);
        hash = PCG();
        float x2 = float(hash) / float(UINT_MAX);

        return float2(x1, x2);
    }
    
    [mutating]
    public float UniformFloat(float a, float b)
    {
        uint hash = PCG();

        float rand = float(hash) / float(UINT_MAX);
        return (rand * (b - a)) + a;
    }

    [mutating]
    public float2 UniformFloat2(float a, float b)
    {
        uint hash = PCG();
        float x1 = float(hash) / float(UINT_MAX);
        hash = PCG();
        float x2 = float(hash) / float(UINT_MAX);

        return (float2(x1, x2) * (b - a)) + a;
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

    [mutating]
    public float3 SampleHenyeyGreenstein(float g, float3 incidentDir)
    {
        const float x1 = UniformFloat();
        const float x2 = UniformFloat();
        const float x3 = UniformFloat();

        float cosTheta;
        if (abs(g) < 1e-5) {
            cosTheta = 2.0 * x1 - 1.0f;
        }
        else {
            float sqrTerm = (1.0 - g * g) / (1.0 - g + 2.0 * g * x2);
            cosTheta = (1.0 + g * g - sqrTerm * sqrTerm) / (2.0 * g);
        }

        float phi = 2.0 * M_PI * x3;

        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        float3 newDir = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

        float3 tangent;
        float3 bitangent;
        CalculateTangents(incidentDir, tangent, bitangent);

        float3 scatteredDir = normalize(newDir.x * tangent + newDir.y * bitangent + newDir.z * incidentDir);

        return scatteredDir;
    }
}