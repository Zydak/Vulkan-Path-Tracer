import Sampler;
import Defines;

public struct Payload
{
    public float3 Origin;
    public float3 Direction;
    public float3 BxDF; // Also contains cosine term
    public float PDF;
    public float3 Emitted; // Surface Emitted Light

    public uint Depth;
    public Sampler Sampler;

    // For tracking the medium the ray is in
    public bool InMedium;
    public float MediumDensity;
    public float MediumAnisotropy;
    public float3 MediumColor;
    public float3 MediumEmissiveColor;

    public bool QueryDistance; // If this flag is set the closest hit shader will return immediately with distance to surface
    public float DistanceToSurface;
};

public struct Vertex
{
    public float3 Position;
    public float3 Normal;
    public float2 TexCoord;
};

public struct UniformBuffer
{
    public float4x4 ViewInverse;
    public float4x4 ProjectionInverse;
    public uint FrameCount;
    public uint Seed;
    public uint SampleCount;
    public uint MaxDepth;
    public float MaxLuminance;
    public float FocusDistance;
    public float DepthOfFieldStrength;
    public float EnvMapRotationAzimuth;
    public float EnvMapRotationAltitude;
    public uint VolumesCount;
    public float EnvironmentIntensity;
};

public float3 Rotate(float3 v, float3 axis, float theta)
{
    float cosTheta = cos(theta);
    float sinTheta = sin(theta);

    float3 normalizedAxis = normalize(axis);

    return (v * cosTheta) + (cross(normalizedAxis, v) * sinTheta) + (normalizedAxis * dot(normalizedAxis, v)) * (1.0f - cosTheta);
}

public bool DoesRayIntersectWithAS(in RaytracingAccelerationStructure as, in float3 origin, in float3 direction)
{
    #ifdef USE_RAY_QUERIES
    {
        RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
        query.__rayQueryInitializeEXT(as, RAY_FLAG_FORCE_OPAQUE, 0xFF, origin, 0.0001, direction, 1000000);
        query.Proceed();
        return query.CommittedStatus() != COMMITTED_NOTHING;
    }
    #else
    {
        RayDesc rayDesc;
        rayDesc.Origin = origin;
        rayDesc.Direction = normalize(direction);
        rayDesc.TMin = 0.00001f;
        rayDesc.TMax = 1000.0f;

        Payload payload;
        payload.Depth = 0;

        TraceRay(as, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0u, 0u, 1u, rayDesc, payload);
        
        return payload.Depth == 0;
    }
    #endif
}

public float GetDistanceToGeometry(in RaytracingAccelerationStructure as, in float3 origin, in float3 direction)
{
    #ifdef USE_RAY_QUERIES
    {
        float distanceToGeometry = -1.0f;
        RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
        query.__rayQueryInitializeEXT(as, RAY_FLAG_FORCE_OPAQUE, 0xFF, origin, 0.00001, direction, 1000000);
        query.Proceed();
        if (query.CommittedStatus() != COMMITTED_NOTHING)
        {
            distanceToGeometry = query.CommittedRayT();
        }

        return distanceToGeometry;
    }
    #else
    {
        RayDesc rayDesc;
        rayDesc.Origin = origin;
        rayDesc.Direction = normalize(direction);
        rayDesc.TMin = 0.00001f;
        rayDesc.TMax = 1000.0f;

        Payload payload;
        payload.QueryDistance = true; // Set Query distance flag

        TraceRay(as, RAY_FLAG_FORCE_OPAQUE, 0xff, 0u, 0u, 1u, rayDesc, payload); // Use shadow miss shader

        return payload.DistanceToSurface;
    }
    #endif
}

public float BalanceHeuristics(float pdfA, float pdfB)
{
    return pdfA / (pdfA + pdfB);
}

public float PowerHeuristics(float pdfA, float pdfB)
{
    return pow(pdfA, 2.0f) / (pow(pdfA, 2.0f) + pow(pdfB, 2.0f));
}

public float2 DirectionToUV(in float3 v)
{
    float gamma = asin(-v.y);
    float theta = atan2(v.x, -v.z);

    float2 uv = float2(theta * M_1_OVER_PI * 0.5F, gamma * M_1_OVER_PI) + 0.5F;
    return uv;
}

// Perlin Noise
// source: https://github.com/stegu/webgl-noise

float3 mod289(float3 x)
{
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

float4 mod289(float4 x)
{
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

float4 permute(float4 x)
{
    return mod289(((x * 34.0) + 10.0) * x);
}

float4 taylorInvSqrt(float4 r)
{
    return 1.79284291400159 - 0.85373472095314 * r;
}

float3 fade(float3 t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Classic Perlin noise
public float cnoise(in float3 P)
{
    float3 Pi0 = floor(P); // Integer part for indexing
    float3 Pi1 = Pi0 + float3(1.0); // Integer part + 1
    Pi0 = mod289(Pi0);
    Pi1 = mod289(Pi1);
    float3 Pf0 = fract(P); // Fractional part for interpolation
    float3 Pf1 = Pf0 - float3(1.0); // Fractional part - 1.0
    float4 ix = float4(Pi0.x, Pi1.x, Pi0.x, Pi1.x);
    float4 iy = float4(Pi0.yy, Pi1.yy);
    float4 iz0 = Pi0.zzzz;
    float4 iz1 = Pi1.zzzz;

    float4 ixy = permute(permute(ix) + iy);
    float4 ixy0 = permute(ixy + iz0);
    float4 ixy1 = permute(ixy + iz1);

    float4 gx0 = ixy0 * (1.0 / 7.0);
    float4 gy0 = fract(floor(gx0) * (1.0 / 7.0)) - 0.5;
    gx0 = fract(gx0);
    float4 gz0 = float4(0.5) - abs(gx0) - abs(gy0);
    float4 sz0 = step(gz0, float4(0.0));
    gx0 -= sz0 * (step(0.0, gx0) - 0.5);
    gy0 -= sz0 * (step(0.0, gy0) - 0.5);

    float4 gx1 = ixy1 * (1.0 / 7.0);
    float4 gy1 = fract(floor(gx1) * (1.0 / 7.0)) - 0.5;
    gx1 = fract(gx1);
    float4 gz1 = float4(0.5) - abs(gx1) - abs(gy1);
    float4 sz1 = step(gz1, float4(0.0));
    gx1 -= sz1 * (step(0.0, gx1) - 0.5);
    gy1 -= sz1 * (step(0.0, gy1) - 0.5);

    float3 g000 = float3(gx0.x, gy0.x, gz0.x);
    float3 g100 = float3(gx0.y, gy0.y, gz0.y);
    float3 g010 = float3(gx0.z, gy0.z, gz0.z);
    float3 g110 = float3(gx0.w, gy0.w, gz0.w);
    float3 g001 = float3(gx1.x, gy1.x, gz1.x);
    float3 g101 = float3(gx1.y, gy1.y, gz1.y);
    float3 g011 = float3(gx1.z, gy1.z, gz1.z);
    float3 g111 = float3(gx1.w, gy1.w, gz1.w);

    float4 norm0 = taylorInvSqrt(float4(dot(g000, g000), dot(g010, g010), dot(g100, g100), dot(g110, g110)));
    g000 *= norm0.x;
    g010 *= norm0.y;
    g100 *= norm0.z;
    g110 *= norm0.w;
    float4 norm1 = taylorInvSqrt(float4(dot(g001, g001), dot(g011, g011), dot(g101, g101), dot(g111, g111)));
    g001 *= norm1.x;
    g011 *= norm1.y;
    g101 *= norm1.z;
    g111 *= norm1.w;

    float n000 = dot(g000, Pf0);
    float n100 = dot(g100, float3(Pf1.x, Pf0.yz));
    float n010 = dot(g010, float3(Pf0.x, Pf1.y, Pf0.z));
    float n110 = dot(g110, float3(Pf1.xy, Pf0.z));
    float n001 = dot(g001, float3(Pf0.xy, Pf1.z));
    float n101 = dot(g101, float3(Pf1.x, Pf0.y, Pf1.z));
    float n011 = dot(g011, float3(Pf0.x, Pf1.yz));
    float n111 = dot(g111, Pf1);

    float3 fade_xyz = fade(Pf0);
    float4 n_z = lerp(float4(n000, n100, n010, n110), float4(n001, n101, n011, n111), fade_xyz.z);
    float2 n_yz = lerp(n_z.xy, n_z.zw, fade_xyz.y);
    float n_xyz = lerp(n_yz.x, n_yz.y, fade_xyz.x);
    return 2.2 * n_xyz;
}