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
    public bool InMedium = false;
    public float MediumDensity = 0.0;
    public float MediumAnisotropy = 1.0;
    public float3 MediumColor = float3(1.0);
    public float3 MediumEmissiveColor = float3(0.0);

    public bool QueryDistance = false; // If this flag is set the closest hit shader will return immediately with distance to surface
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
    #define USE_RAY_QUERIES

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

public float BalanceHeuristics(float pdfA, float pdfB)
{
    return pdfA / (pdfA + pdfB);
}

public float PowerHeuristics(float pdfA, float pdfB)
{
    return pow(pdfA, 2.0f) / (pow(pdfA, 2.0f) + pow(pdfB, 2.0f));
}