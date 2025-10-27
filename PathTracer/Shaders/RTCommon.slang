import Sampler;
import Defines;
import Bindings;

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

    // This is an RGB renderer, but to simulate the atmosphere only a single channel has to be used - the ray has to be split.
    // This variable indicates which channel that is. -1 means all channels are being currently tracked so the ray hasn't been split
    // 0 = R, 1 = G, 2 = B.
    public int ColorChannel;

    public uint TriangleIdx; // The index of the hit triangle, needed for emissive meshes MIS
    public uint InstanceIdx;

    public uint VolumeDepth; // How many scatterings have occurred in the volumes
};

public float3 Rotate(float3 v, float3 axis, float theta)
{
    float cosTheta = cos(theta);
    float sinTheta = sin(theta);

    float3 normalizedAxis = normalize(axis);

    return (v * cosTheta) + (cross(normalizedAxis, v) * sinTheta) + (normalizedAxis * dot(normalizedAxis, v)) * (1.0f - cosTheta);
}

public bool DoesRayIntersectWithAS(in RaytracingAccelerationStructure as, in float3 origin, in float3 direction, out uint hitTriangleIdx, out uint hitInstanceIndex)
{
    hitTriangleIdx = 0;
    hitInstanceIndex = 0;

    #ifdef USE_RAY_QUERIES
    {
        RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
        query.__rayQueryInitializeEXT(as, RAY_FLAG_FORCE_OPAQUE, 0xFF, origin, 0.0001, direction, 1000000);
        query.Proceed();
        if (query.CommittedStatus() != COMMITTED_NOTHING)
        {
            hitTriangleIdx = query.CommittedPrimitiveIndex();
            hitInstanceIndex = query.CommittedInstanceIndex();
            return true;
        }
        return false;
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

        hitTriangleIdx = payload.TriangleIdx;
        hitInstanceIndex = payload.InstanceIdx;
        
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
    float gamma = asin(v.y);
    float theta = atan2(v.x, -v.z);

    float2 uv = float2(theta * M_1_OVER_PI * 0.5F, gamma * M_1_OVER_PI) + 0.5F;
    return uv;
}

/// [https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html] source
public float3 Blackbody(float temperature)
{
    // Temperature is in Kelvin
    float temp = temperature / 100.0f;

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    // Red
    if (temp <= 66.0f) {
        r = 255.0f;
    } else {
        r = 329.698727446 * pow(temp - 60.0f, -0.1332047592f);
    }

    // Green
    if (temp <= 66.0f) {
        g = 99.4708025861 * log(temp) - 161.1195681661;
    } else {
        g = 288.1221695283 * pow(temp - 60.0f, -0.0755148492f);
    }

    // Blue
    if (temp >= 66.0f) {
        b = 255.0f;
    } else if (temp <= 19.0f) {
        b = 0.0f;
    } else {
        b = 138.5177312231 * log(temp - 10.0f) - 305.0447927307;
    }

    return clamp(float3(r, g, b) / 255.0f, 0.0f, 1.0f);
}

public float2 IntersectSphere(in float3 rayOrigin, in float3 rayDirection, in float3 sphereCenter, in float sphereRadius)
{
    rayOrigin -= sphereCenter; // Translate ray origin to sphere-centered coordinates

    // Calculate the intersection of the ray with the sphere
    float a = dot(rayDirection, rayDirection);
    float b = 2.0 * dot(rayOrigin, rayDirection);
    float c = dot(rayOrigin, rayOrigin) - sphereRadius * sphereRadius;

    float discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0)
    {
        return float2(-1.0, -1.0); // No intersection
    }

    float t0 = (-b - sqrt(discriminant)) / (2.0 * a);
    float t1 = (-b + sqrt(discriminant)) / (2.0 * a);
    return float2(t0, t1);
}

// Phase Functions
//

public float RayleighPhase(float3 V, float3 L)
{
    float cosTheta = dot(V, L);
    return (3.0f / (16.0f * M_PI)) * (1.0f + cosTheta * cosTheta);
}

public float PhaseMie(float3 V, float3 L, float g = 0.85f)
{
    float cosTheta = dot(V, L);
	g = min(g, 0.9381);
	float k = 1.55*g - 0.55*g*g*g;
	float kcosth = k*cosTheta;
	return (1 - k*k) / ((4 * M_PI) * (1-kcosth) * (1-kcosth));
}

public float PhaseHenyeyGreenstein(float3 V, float3 L, in float g)
{
    if (g == 0.0f)
        return 1.0f / (4.0f * M_PI);
    
    float cosTheta = dot(V, L);
    return (1.0f / (4.0f * M_PI)) * ((1.0f - g * g) / pow(1.0f + g * g - 2.0f * g * cosTheta, 1.5f));
}

public float PhaseDraine(float3 V, float3 L, in float g, in float a)
{
    float costTheta = dot(V, L);

    return ((1 - g*g)*(1 + a*costTheta*costTheta))/(4.*(1 + (a*(1 + 2*g*g))/3.) * M_PI * pow(1 + g*g - 2*g*costTheta,1.5));
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