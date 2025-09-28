import Defines;
import Bindings;

public uint PCG_HASH(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float3 Rotate(float3 v, float3 axis, float theta)
{
    float cosTheta = cos(theta);
    float sinTheta = sin(theta);

    float3 normalizedAxis = normalize(axis);

    return (v * cosTheta) + (cross(normalizedAxis, v) * sinTheta) + (normalizedAxis * dot(normalizedAxis, v)) * (1.0f - cosTheta);
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
    public float3 SampleRayleigh(float3 incidentDir)
    {
        float2 rand = UniformFloat2();
        
        float u = -pow(2 * (2 * rand.x - 1) + sqrt(4 * pow(2 * rand.x - 1, 2) + 1), 1.0f / 3.0f);
        float cosTheta = u - (1.0f / u);
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
    public float3 SampleDraine(float3 incidentDir, float g, float a)
    {        
        float2 rand = UniformFloat2();

        float cosTheta;
        if (abs(g) < 1e-5) {
            cosTheta = 2.0 * rand.x - 1.0f;
        }
        else if (abs(a) < 1e-5)
        {
            float sqrTerm = (1.0 - g * g) / (1.0 - g + 2.0 * g * rand.x);
            cosTheta = (1.0 + g * g - sqrTerm * sqrTerm) / (2.0 * g);
        }
        else
        {
            const float g2 = g * g;
            const float g3 = g * g2;
            const float g4 = g2 * g2;
            const float g6 = g2 * g4;
            const float pgp1_2 = (1 + g2) * (1 + g2);
            const float T1 = (-1 + g2) * (4 * g2 + a * pgp1_2);
            const float T1a = -a + a * g4;
            const float T1a3 = T1a * T1a * T1a;
            const float T2 = -1296 * (-1 + g2) * (a - a * g2) * (T1a) * (4 * g2 + a * pgp1_2);
            const float T3 = 3 * g2 * (1 + g * (-1 + 2 * rand.x)) + a * (2 + g2 + g3 * (1 + 2 * g2) * (-1 + 2 * rand.x));
            const float T4a = 432 * T1a3 + T2 + 432 * (a - a * g2) * T3 * T3;
            const float T4b = -144 * a * g2 + 288 * a * g4 - 144 * a * g6;
            const float T4b3 = T4b * T4b * T4b;
            const float T4 = T4a + sqrt(-4 * T4b3 + T4a * T4a);
            const float T4p3 = pow(T4, 1.0 / 3.0);
            const float T6 = (2 * T1a + (48 * pow(2, 1.0 / 3.0) *
                (-(a * g2) + 2 * a * g4 - a * g6)) / T4p3 + T4p3 / (3. * pow(2, 1.0 / 3.0))) / (a - a * g2);
            const float T5 = 6 * (1 + g2) + T6;
            cosTheta = (1 + g2 - pow(-0.5 * sqrt(T5) + sqrt(6 * (1 + g2) - (8 * T3) / (a * (-1 + g2) * sqrt(T5)) - T6) / 2., 2)) / (2. * g);
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
    public float3 SampleHGPlusDraine(float3 incidentDir, float d)
    {
        const float GHG = exp(-(0.0990567f / (d - 1.67154)));
        const float GD = exp(-(2.20679f / (d + 3.91029)) - 0.428934);
        const float ALPHA_D = exp(3.62489f - (8.29288f / (d + 5.52825)));
        const float W_D = exp(-(0.599085f / (d - 0.641583)) - 0.665888);

        float u = UniformFloat();
        if (u < W_D)
            return SampleHenyeyGreenstein(incidentDir, GHG);
        else
            return SampleDraine(incidentDir, GD, ALPHA_D);
    }

    [mutating]
    public void ImportanceSampleEnvMap(out float3 toLight, out float4 outValue)
    {
        // Uniformly pick a texel index idx in the environment map
        float3 xi = UniformFloat3();

        uint width;
        uint height;
        uEnvMapTexture.GetDimensions(width, height);

        uint size = width * height;
        uint idx = min(uint(xi.x * float(size)), size - 1);

        // Fetch the entry for that texel, containing the importance and the texel alias
        AliasMapEntry entry = uEnvAliasMap[idx];

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

        toLight = float3(sinPhi * sinTheta, -cosTheta, -cosPhi * sinTheta);

        float azimuth = uUBO.SkyRotationAzimuth / 180.0f * M_PI; // Convert degrees to radians
        float altitude = uUBO.SkyRotationAltitude / 180.0f * M_PI; // Convert degrees to radians
        toLight = Rotate(toLight, float3(0.0f, 1.0f, 0.0f), azimuth);
        toLight = Rotate(toLight, float3(1.0f, 0.0f, 0.0f), altitude);

        // Lookup the environment value using computed uvs
        outValue = uEnvMapTexture.SampleLevel(uTextureSampler, {u, v}, 0);
        outValue.rgb *= uUBO.EnvironmentIntensity;
    }

    [mutating]
    public float SampleScatteringDistance(float density)
    {
        return -log(UniformFloat()) / density;
    }

    [mutating]
    public void SampleSunDisk(float sunTheta, out float3 toLight, out float4 colorPDF)
    {
        float azimuth = uUBO.SkyRotationAzimuth / 180.0f * M_PI; // Convert degrees to radians
        float altitude = uUBO.SkyRotationAltitude / 180.0f * M_PI; // Convert degrees to radians
        float3 sunDir = Rotate(float3(0.0f, 0.0f, -1.0f), float3(1.0f, 0.0f, 0.0f), altitude); // Start from -Z (front)
        sunDir = Rotate(sunDir, float3(0.0f, 1.0f, 0.0f), azimuth);

        float cosThetaMax = cos(sunTheta);

        float phi = 2.0 * M_PI * UniformFloat();
        float cosTheta = lerp(cosThetaMax, 1.0, UniformFloat());
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

        // Local direction in cone coordinates (z = cone axis)
        float3 localDir = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

        // Build an orthonormal basis around sunDir
        float3 w = normalize(sunDir);
        float3 up = abs(w.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
        float3 u = normalize(cross(up, w));
        float3 v = cross(w, u);

        // Transform local direction to world space
        float3 dir = u * localDir.x + v * localDir.y + w * localDir.z;

        float solidAngle = 2.0f * M_PI * (1.0 - cosThetaMax);
        colorPDF.a = 1.0 / solidAngle;

        colorPDF.rgb = 2e5 * uUBO.SunColor.rgb * uUBO.EnvironmentIntensity;

        toLight = dir;
    }

    [mutating]
    public void ImportanceSampleSky(out float3 toLight, out float4 outValue)
    {
        #ifdef ENABLE_ATMOSPHERE
        {
            SampleSunDisk(0.004675f, toLight, outValue); // For now hardcoded to 0.004675 radians
        }
        #else
        {
            ImportanceSampleEnvMap(toLight, outValue);
        }
        #endif
    }
}