import Sampler;
import RTCommon;
import Defines;
import Bindings;

// Default Scattering coefficients at sea level for red, green and blue wavelengths on earth in meters
public static const float3 C_RAYLEIGH = float3(5.802, 13.558, 33.100) * 1e-6;
public static const float3 C_MIE_SCATTERING = float3(3.996) * 1e-6;
public static const float3 C_MIE_ABSORPTION = float3(4.40) * 1e-6;
public static const float3 C_MIE = C_MIE_SCATTERING + C_MIE_ABSORPTION;
public static const float3 C_OZONE = float3(0.650,  1.881,  0.085) * 1e-6;

public float GetAtmosphereHeight(float3 position)
{
    return length(position - uUBO.PlanetPosition.xyz) - uUBO.PlanetRadius;
}

public float GetRayleighDensity(float height)
{
    return exp(-height / uUBO.RayleighDensityFalloff);
}

public float GetMIEDensity(float height)
{
    return exp(-height / uUBO.MieDensityFalloff);
}

public float GetOzoneDensity(float height)
{
    return exp(-(abs(height - uUBO.OzonePeak) / uUBO.OzoneDensityFalloff));
}

public float3 CalculateTransmittanceThroughAtmosphere(inout Sampler sampler, float3 rayOrigin, float3 rayDirection, int colorChannel)
{
    // Check if occluded by the planet
    float2 planetIntersection = IntersectSphere(rayOrigin, rayDirection, uUBO.PlanetPosition.xyz, uUBO.PlanetRadius);
    if (planetIntersection.y > 0.0f)
    {
        return 0.0f; // Occluded by the planet
    }

    float2 atmosphereIntersection = IntersectSphere(rayOrigin, rayDirection, uUBO.PlanetPosition.xyz, uUBO.PlanetRadius + uUBO.AtmosphereHeight);
    float tMin = max(atmosphereIntersection.x, 0.0f);
    float tMax = atmosphereIntersection.y;

    if (tMax < 0.0f)
    {
        // Ray is completely outside the atmosphere
        return 1.0f;
    }

    float scatteringCoefficientRayleigh = C_RAYLEIGH[colorChannel] * uUBO.RayleighScatteringCoefficientMultiplier[colorChannel];
    float scatteringCoefficientMie = C_MIE[colorChannel] * uUBO.MieScatteringCoefficientMultiplier[colorChannel];
    float scatteringCoefficientOzone = C_OZONE[colorChannel] * uUBO.OzoneAbsorptionCoefficientMultiplier[colorChannel];

    float rayleighDensitySea = GetRayleighDensity(0.0f) * scatteringCoefficientRayleigh; // Density at sea level
    float mieDensitySea = GetMIEDensity(0.0f) * scatteringCoefficientMie; // Density at sea level
    float ozoneDensitySea = GetOzoneDensity(uUBO.OzonePeak) * scatteringCoefficientOzone; // Density at peak altitude

    float majorant = rayleighDensitySea + mieDensitySea + ozoneDensitySea;

    if (majorant <= 0.0f)
    {
        // No atmosphere
        return 1.0f;
    }

    float t = 0.0f;
    float3 transmittance = float3(0.0f);
    transmittance[colorChannel] = 1.0f;
    for (int i = 0; i < 1000; i++)
    {
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

        // Russian roulette termination
        float p = transmittance[colorChannel];
        if (sampler.UniformFloat() > p)
        {
            transmittance[colorChannel] = 0.0f;
            break;
        }
        transmittance[colorChannel] /= p;
    }

    return transmittance;
}

public enum AtmosphereComponent
{
    None = -1,
    Rayleigh = 0,
    Mie = 1,
    Ozone = 2
};

public float SampleAtmosphereScatterDistance(inout Sampler sampler, float3 rayOrigin, float3 rayDirection, int colorChannel, out AtmosphereComponent atmosphereComponentHit)
{
    float2 atmosphereIntersection = IntersectSphere(rayOrigin, rayDirection, uUBO.PlanetPosition.xyz, uUBO.PlanetRadius + uUBO.AtmosphereHeight);

    float tMinAtmosphere = max(atmosphereIntersection.x, 0.0f);
    float tMaxAtmosphere = atmosphereIntersection.y;

    atmosphereComponentHit = AtmosphereComponent::None; // Default to no hit

    float2 planetIntersection = IntersectSphere(rayOrigin, rayDirection, uUBO.PlanetPosition.xyz, uUBO.PlanetRadius);

    float tMinPlanet = planetIntersection.x;
    float tMaxPlanet = planetIntersection.y;

    if (tMaxAtmosphere < 0.0f)
    {
        // No intersection with atmosphere
        return -1.0f;
    }

    float scatteringCoefficientRayleigh = C_RAYLEIGH[colorChannel] * uUBO.RayleighScatteringCoefficientMultiplier[colorChannel];
    float scatteringCoefficientMie = C_MIE[colorChannel] * uUBO.MieScatteringCoefficientMultiplier[colorChannel];
    float extinctionCoefficientOzone = C_OZONE[colorChannel] * uUBO.OzoneAbsorptionCoefficientMultiplier[colorChannel];

    float majorant = GetRayleighDensity(0.0f) * scatteringCoefficientRayleigh + GetMIEDensity(0.0f) * scatteringCoefficientMie + GetOzoneDensity(uUBO.OzonePeak) * extinctionCoefficientOzone; // Density at sea level

    if (majorant <= 0.0f)
    {
        // No atmosphere
        return -1.0f;
    }

    float t = tMinAtmosphere;
    for (int i = 0; i < 1000; i++)
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
        float densityOzone = GetOzoneDensity(height) * extinctionCoefficientOzone;
        float density = densityRayleigh + densityMie + densityOzone;

        if (density / majorant < sampler.UniformFloat())
        {
            continue; // Null collision
        }

        float rayleighProb = densityRayleigh / density;
        float mieProb = densityMie / density;
        float ozoneProb = densityOzone / density;

        // Stochastically pick which component was hit
        float x = sampler.UniformFloat();
        if (x <= rayleighProb)
        {
            atmosphereComponentHit = AtmosphereComponent::Rayleigh;
        }
        else if (x <= rayleighProb + mieProb)
        {
            atmosphereComponentHit = AtmosphereComponent::Mie;
        }
        else
        {
            atmosphereComponentHit = AtmosphereComponent::Ozone;
        }

        return t; // Real collision, t found
    }

    return -1.0f; // no t found
}