public static const uint UINT_MAX = 4294967295;
public static const float M_PI = 3.1415926535897F;

public struct Sampler
{
    uint m_Seed;

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
    public float3 UniformFloat3()
    {
        uint hash = PCG();
        float x1 = float(hash) / float(UINT_MAX);
        hash = PCG();
        float x2 = float(hash) / float(UINT_MAX);
        hash = PCG();
        float x3 = float(hash) / float(UINT_MAX);

        return float3(x1, x2, x3);
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
    public float3 UniformFloat3(float a, float b)
    {
        uint hash = PCG();
        float x1 = float(hash) / float(UINT_MAX);
        hash = PCG();
        float x2 = float(hash) / float(UINT_MAX);
        hash = PCG();
        float x3 = float(hash) / float(UINT_MAX);

        return (float3(x1, x2, x3) * (b - a)) + a;
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