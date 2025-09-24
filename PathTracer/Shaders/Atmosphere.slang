import Sampler;
import RTCommon;
import Defines;

// Everything is in meters
public static const float PLANET_RADIUS = 6371000;
public static const float3 PLANET_CENTER = float3(0, PLANET_RADIUS + 1000, 0); // Offset by 1km down, +Y is down in vulkan
public static const float ATMOSPHERE_HEIGHT = 100000;

public static const float3 C_RAYLEIGH = float3(5.802, 13.558, 33.100) * 1e-6;
public static const float3 C_MIE = float3(3.996,  3.996,  3.996) * 1e-6;
public static const float3 C_OZONE = float3(0.650,  1.881,  0.085) * 1e-6;

public float GetAtmosphereHeight(float3 position)
{
    return length(position - PLANET_CENTER) - PLANET_RADIUS;
}

public float GetRayleighDensity(float height)
{
    const float RAYLEIGH_SCALE_HEIGHT = 8000.0f; // 8km
    return exp(-height / RAYLEIGH_SCALE_HEIGHT);
}

public float GetMIEDensity(float height)
{
    const float MIE_SCALE_HEIGHT = 1200.0f; // 1.2km
    return exp(-height / MIE_SCALE_HEIGHT);
}

public float GetOzoneDensity(float height)
{
    float heightPeak = 22000.0; // Peak density at 22km
    float sigma = 8000.0;       // Thickness control
    float x = (height - heightPeak) / sigma;
    return exp(-0.5 * x * x);
}

public float CalculateTransmittanceThroughAtmosphere(inout Sampler sampler, float3 rayOrigin, float3 rayDirection, float scatteringCoefficientRayleigh, float scatteringCoefficientMie, float scatteringCoefficientOzone)
{
    // Check if occluded by the planet
    float2 planetIntersection = IntersectSphere(rayOrigin, rayDirection, PLANET_CENTER, PLANET_RADIUS);
    if (planetIntersection.y > 0.0f)
    {
        return 0.0f; // Occluded by the planet
    }

    float2 atmosphereIntersection = IntersectSphere(rayOrigin, rayDirection, PLANET_CENTER, PLANET_RADIUS + ATMOSPHERE_HEIGHT);
    float tMin = max(atmosphereIntersection.x, 0.0f);
    float tMax = atmosphereIntersection.y;

    if (tMax < 0.0f)
    {
        // Ray is completely outside the atmosphere
        return 1.0f;
    }

    float t = 0.0f;
    float transmittance = 1.0f;
    for (int i = 0; i < 100; i++)
    {
        float rayleighDensitySea = GetRayleighDensity(0.0f) * scatteringCoefficientRayleigh; // Density at sea level
        float mieDensitySea = GetMIEDensity(0.0f) * scatteringCoefficientMie; // Density at sea level
        float ozoneDensitySea = GetOzoneDensity(0.0f) * scatteringCoefficientOzone; // Density at sea level

        float majorant = rayleighDensitySea + mieDensitySea + ozoneDensitySea;
        float deltaT = -log(1.0f - sampler.UniformFloat()) / majorant;

        t += deltaT;

        if (t >= tMax - tMin)
        {
            break; // Ray exited atmosphere
        }

        float height = GetAtmosphereHeight(rayOrigin + rayDirection * (t + tMin));

        if (height < 0.0f)
        {
            break; // Below the surface
        }

        float rayleighDensity = GetRayleighDensity(height) * scatteringCoefficientRayleigh;
        float mieDensity = GetMIEDensity(height) * scatteringCoefficientMie;
        float ozoneDensity = GetOzoneDensity(height) * scatteringCoefficientOzone;

        transmittance *= 1.0f - (rayleighDensity + mieDensity + ozoneDensity) / majorant;
    }

    return transmittance;
}

public float SampleAtmosphereScatterDistance(inout Sampler sampler, float3 rayOrigin, float3 rayDirection, float scatteringCoefficientRayleigh, float scatteringCoefficientMie)
{
    float2 atmosphereIntersection = IntersectSphere(rayOrigin, rayDirection, PLANET_CENTER, PLANET_RADIUS + ATMOSPHERE_HEIGHT);

    float tMinAtmosphere = max(atmosphereIntersection.x, 0.0f);
    float tMaxAtmosphere = atmosphereIntersection.y;

    float2 planetIntersection = IntersectSphere(rayOrigin, rayDirection, PLANET_CENTER, PLANET_RADIUS);

    float tMinPlanet = planetIntersection.x;
    float tMaxPlanet = planetIntersection.y;

    if (tMaxAtmosphere < 0.0f)
    {
        // No intersection with atmosphere
        return -1.0f;
    }

    float majorant = GetRayleighDensity(0.0f) * scatteringCoefficientRayleigh + GetMIEDensity(0.0f) * scatteringCoefficientMie; // Density at sea level

    float t = tMinAtmosphere;
    for (int i = 0; i < 100; i++)
    {
        float deltaT = -log(1.0f - sampler.UniformFloat()) / majorant;
        t += deltaT;

        // Check if the new position is still within the atmosphere
        if (t >= tMaxAtmosphere)
        {
            break; // Ray exited atmosphere
        }

        if (tMinPlanet > 0.0f && t >= tMinPlanet)
        {
            break; // Ray hit the planet
        }

        float3 samplePosition = rayOrigin + rayDirection * t;

        float height = GetAtmosphereHeight(samplePosition);

        float densityRayleigh = GetRayleighDensity(height) * scatteringCoefficientRayleigh;
        float densityMie = GetMIEDensity(height) * scatteringCoefficientMie;
        float density = densityRayleigh + densityMie;

        if (density / majorant < sampler.UniformFloat())
        {
            continue; // Null collision
        }

        return t; // Real collision, t found
    }

    return -1.0f; // no t found
}

public float3 SampleSunDisk(float3 sunDir, float sunTheta, float2 xi, out float pdf)
{
    float cosThetaMax = cos(sunTheta);

    float phi = 2.0 * M_PI * xi.x;
    float cosTheta = lerp(cosThetaMax, 1.0, xi.y);
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
    pdf = 1.0 / solidAngle;

    return dir;
}