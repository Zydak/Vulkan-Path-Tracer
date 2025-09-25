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

public float3 CalculateTransmittanceThroughAtmosphere(inout Sampler sampler, float3 rayOrigin, float3 rayDirection, int colorChannel)
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

    float scatteringCoefficientRayleigh = C_RAYLEIGH[colorChannel];
    float scatteringCoefficientMie = C_MIE[colorChannel];
    float scatteringCoefficientOzone = C_OZONE[colorChannel];

    float t = 0.0f;
    float3 transmittance = float3(0.0f);
    transmittance[colorChannel] = 1.0f;
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

        transmittance[colorChannel] *= 1.0f - (rayleighDensity + mieDensity + ozoneDensity) / majorant;
    }

    return transmittance;
}

// atmosphereComponentHit: 0 = Rayleigh, 1 = Mie, -1 = no hit
public float SampleAtmosphereScatterDistance(inout Sampler sampler, float3 rayOrigin, float3 rayDirection, int colorChannel, out int atmosphereComponentHit)
{
    float2 atmosphereIntersection = IntersectSphere(rayOrigin, rayDirection, PLANET_CENTER, PLANET_RADIUS + ATMOSPHERE_HEIGHT);

    float tMinAtmosphere = max(atmosphereIntersection.x, 0.0f);
    float tMaxAtmosphere = atmosphereIntersection.y;

    atmosphereComponentHit = -1; // Default to no hit

    float2 planetIntersection = IntersectSphere(rayOrigin, rayDirection, PLANET_CENTER, PLANET_RADIUS);

    float tMinPlanet = planetIntersection.x;
    float tMaxPlanet = planetIntersection.y;

    if (tMaxAtmosphere < 0.0f)
    {
        // No intersection with atmosphere
        return -1.0f;
    }

    float scatteringCoefficientRayleigh = C_RAYLEIGH[colorChannel];
    float scatteringCoefficientMie = C_MIE[colorChannel];

    // Ozone doesn't scatter light, it only absorbs it, so it's only included in transmittance calculations
    // No point in wasting time on an empty collision
    // float scatteringCoefficientOzone = C_OZONE[colorChannel];

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

        float rayleighProb = densityRayleigh / density;
        float mieProb = densityMie / density;

        // Stochastically pick which component was hit
        if (sampler.UniformFloat() <= rayleighProb)
        {
            atmosphereComponentHit = 0; // Rayleigh
        }
        else if (sampler.UniformFloat() <= rayleighProb + mieProb)
        {
            atmosphereComponentHit = 1; // Mie
        }

        return t; // Real collision, t found
    }

    return -1.0f; // no t found
}