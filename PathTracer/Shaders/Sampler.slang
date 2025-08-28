import Defines;

public struct AliasMapEntry
{
    public uint Alias;
    public float Importance;
};

public uint PCG_HASH(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

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
    public float3 RandomHemisphereVecCosineWeight(in float3 normal)
    {
        return normalize(RandomSphereVec() + normal);
    }

    // Reference https://jcgt.org/published/0007/04/01/paper.pdf
    [mutating]
    public float3 GGXSampleAnisotopic(float3 Ve, float Ax, float Ay)
    {
        float u1 = UniformFloat();
        float u2 = UniformFloat();

        float3 Vh = normalize(float3(Ax * Ve.x, Ay * Ve.y, abs(Ve.z)));

        float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
        float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * (1.0f / sqrt(lensq)) : float3(1, 0, 0);
        float3 T2 = cross(Vh, T1);

        float r = sqrt(u1);
        float phi = 2.0 * M_PI * u2;
        float t1 = r * cos(phi);
        float t2 = r * sin(phi);
        float s = 0.5 * (1.0 + Vh.z);
        t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

        float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

        float3 Ne = normalize(float3(Ax * Nh.x, Ay * Nh.y, max(0.0, Nh.z)));

        return Ne;
    }

    [mutating]
    public float3 SampleHenyeyGreenstein(float3 incidentDir, float G)
    {
        float2 rand = UniformFloat2();
        float cosTheta;
        if (abs(G) < 1e-5) {
            cosTheta = 2.0 * rand.x - 1.0f;
        }
        else {
            float sqrTerm = (1.0 - G * G) / (1.0 - G + 2.0 * G * rand.x);
            cosTheta = (1.0 + G * G - sqrTerm * sqrTerm) / (2.0 * G);
        }

        float phi = 2.0 * M_PI * rand.y;

        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        float3 newDir = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

        // new dir is created in tangent space, so rotate it by incidentDir
        float3 up = abs(incidentDir.y) < 0.9999999 ? float3(0, 1, 0) : float3(0, 0, 1);
        float3 tangent = normalize(cross(up, incidentDir));
        float3 bitangent = cross(incidentDir, tangent);
        float3 scatteredDir = normalize(newDir.x * tangent + newDir.y * bitangent + newDir.z * incidentDir);

        return scatteredDir;
    }

    [mutating]
    public void ImportanceSampleEnvMap(in Texture2D hdrTexture, in SamplerState sampler, in StructuredBuffer<AliasMapEntry> uAccels, float azimuth, float altitude, out float3 toLight, out float4 outValue)
    {
        // Uniformly pick a texel index idx in the environment map
        float3 xi = UniformFloat3();

        uint width;
        uint height;
        hdrTexture.GetDimensions(width, height);

        uint size = width * height;
        uint idx = min(uint(xi.x * float(size)), size - 1);

        // Fetch the entry for that texel, containing the importance and the texel alias
        AliasMapEntry entry = uAccels[idx];

        uint envIdx;

        if (xi.y < entry.Importance)
        {
            // If the random variable is lower than the importance, we directly pick
            // this texel, and renormalize the random variable for later use.
            envIdx = idx;
            xi.y /= entry.Importance;
        }
        else
        {
            // Otherwise we pick the alias of the texel and renormalize the random variable
            envIdx = entry.Alias;
            xi.y = (xi.y - entry.Importance) / (1.0f - entry.Importance);
        }

        // Compute the 2D integer coordinates of the texel
        const uint px = envIdx % width;
        uint py = envIdx / width;

        // Uniformly sample the solid angle subtended by the pixel.
        // Generate both the UV for texture lookup and a direction in spherical coordinates
        const float u = float(px + xi.y) / float(width);
        const float phi = u * (2.0f * M_PI) - M_PI;
        float sinPhi = sin(phi);
        float cosPhi = cos(phi);

        const float stepTheta = M_PI / float(height);
        const float theta0 = float(py) * stepTheta;
        const float cosTheta = cos(theta0) * (1.0f - xi.z) + cos(theta0 + stepTheta) * xi.z;
        const float theta = acos(cosTheta);
        const float sinTheta = sin(theta);
        const float v = theta * M_1_OVER_PI;

        toLight = float3(cosPhi * sinTheta, cosTheta, sinPhi * sinTheta);

        // Lookup the environment value using computed uvs
        outValue = hdrTexture.SampleLevel(sampler, {u, v}, 0);
    }

    [mutating]
    public float SampleScatteringDistance(float density)
    {
        return -log(UniformFloat()) / density;
    }
}