import Sampler;
import Defines;

public struct Payload
{
    public float3 Origin;
    public float3 Direction;
    public float3 Contribution; // BRDF * cosine / PDF
    public float3 Emitted; // Surface Emitted Light

    public uint Depth;
    public Sampler Sampler;

    // For tracking the medium the ray is in
    public bool InMedium = false;
    public float MediumDensity = 0.0;
    public float MediumAnisotropy = 1.0;
    public float3 MediumColor = float3(1.0);
    public float3 MediumEmissiveColor = float3(0.0);
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
};

public float3 Rotate(float3 v, float3 axis, float theta)
{
    float cosTheta = cos(theta);
    float sinTheta = sin(theta);

    float3 normalizedAxis = normalize(axis);

    return (v * cosTheta) + (cross(normalizedAxis, v) * sinTheta) + (normalizedAxis * dot(normalizedAxis, v)) * (1.0f - cosTheta);
}

struct HitQueryPayload
{
    bool Hit;
}

public bool DoesRayIntersectWithAS(in RaytracingAccelerationStructure as, in float3 origin, in float3 direction)
{
    RayDesc rayDesc;
    rayDesc.Origin = origin;
    rayDesc.Direction = normalize(direction);
    rayDesc.TMin = 0.00001f;
    rayDesc.TMax = 1000.0f;

    HitQueryPayload payload;
    payload.Hit = true;

    TraceRay(as, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0u, 0u, 1u, rayDesc, payload);

    return payload.Hit;
}