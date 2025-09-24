import RTCommon;
import Defines;
import Sampler;
import Atmosphere;

[[vk::binding(2, 0)]] ConstantBuffer<UniformBuffer> uUBO;

[[vk::binding(10, 0)]] SamplerState uSampler;

[[vk::image_format("rgba32f")]]
[[vk::binding(15, 0)]] Texture2D uEnvMapTexture;

#define ENABLE_ATMOSPHERE

void ScatterInAtmosphere(inout Payload payload)
{
    // Choose one of the color, R G or B
    float r = payload.Sampler.UniformFloat();
    int colorChannel = 0;
    if (r < 0.333333f)
        colorChannel = 0;
    else if (r < 0.666666f)
        colorChannel = 1;
    else
        colorChannel = 2;

    payload.Emitted = float3(0.0f);

    if (GetAtmosphereHeight(payload.Origin) < 0.0f)
    {
        return; // Below the surface
    }

    float azimuth = uUBO.EnvMapRotationAzimuth / 180.0f * M_PI; // Convert degrees to radians
    float altitude = uUBO.EnvMapRotationAltitude / 180.0f * M_PI; // Convert degrees to radians
    float3 SUN_DIRECTION = Rotate(float3(0.0f, 0.0f, -1.0f), float3(0.0f, 1.0f, 0.0f), azimuth); // Start from -Z (front)
    SUN_DIRECTION = Rotate(SUN_DIRECTION, float3(1.0f, 0.0f, 0.0f), altitude);

    float R_COEF;
    float M_COEF;
    float O_COEF;
    if (colorChannel == 0)
    {
        R_COEF = C_RAYLEIGH.r;
        M_COEF = C_MIE.r;
        O_COEF = C_OZONE.r;
    }
    else if (colorChannel == 1)
    {
        R_COEF = C_RAYLEIGH.g;
        M_COEF = C_MIE.g;
        O_COEF = C_OZONE.g;
    }
    else
    {
        R_COEF = C_RAYLEIGH.b;
        M_COEF = C_MIE.b;
        O_COEF = C_OZONE.b;
    }

    float sum = 0.0f;
    float throughput = 1.0f;
    for (int i = 0; i < 100; i++)
    {
        float deltaT = SampleAtmosphereScatterDistance(payload.Sampler, payload.Origin, payload.Direction, R_COEF, M_COEF);
        if (deltaT <= 0.0f)
            break; // Exited the atmosphere

        payload.Origin += deltaT * payload.Direction;

        float3 sunDirSampled;
        float sunPdf;
        sunDirSampled = SampleSunDisk(SUN_DIRECTION, 0.004675f, payload.Sampler.UniformFloat2(), sunPdf);

        float sunRadiance = 2e5;

        // Compute transmittance to the sun
        float transmittanceToSun = CalculateTransmittanceThroughAtmosphere(payload.Sampler, payload.Origin, sunDirSampled, R_COEF, M_COEF, O_COEF);

        // Stochastically choose between Rayleigh and Mie
        float3 newDir;

        float rayleighDensity = GetRayleighDensity(GetAtmosphereHeight(payload.Origin)) * R_COEF;
        float mieDensity = GetMIEDensity(GetAtmosphereHeight(payload.Origin)) * M_COEF;
        float densitySum = rayleighDensity + mieDensity;

        float rayleighProb = rayleighDensity / densitySum;
        float mieProb = mieDensity / densitySum;

        if (payload.Sampler.UniformFloat() <= rayleighProb)
        {
            // Rayleigh
            newDir = payload.Sampler.SampleRayleigh(payload.Direction);
            sum += throughput * RayleighPhase(payload.Direction, sunDirSampled) * transmittanceToSun * (sunRadiance / sunPdf);

            // Rayleigh is perfectly importance sampled and has no absorption so throughput is not changed
            // throughput *= RayleighPhase(dot(payload.Direction, newDir)) / RayleighPhase(dot(payload.Direction, newDir));
        }
        else if (payload.Sampler.UniformFloat() <= rayleighProb + mieProb)
        {
            // Mie
            float color = 1.0f - (M_COEF * 1.1);
            newDir = payload.Sampler.SampleHenyeyGreenstein(payload.Direction, 0.85f); // Sample henyey since it's a pretty close match
            sum += throughput * color * PhaseMie(payload.Direction, sunDirSampled) * transmittanceToSun * (sunRadiance / sunPdf);

            // Mie is no longer perfectly importance sampled, MIE phase function was used but the sampling was done with Henyey-Greenstein
            // MIE also has absorption
            throughput *= color * PhaseMie(payload.Direction, newDir) / PhaseHenyeyGreenstein(payload.Direction, newDir);
        }

        payload.Direction = newDir;
    }

    // Very professional
    if (colorChannel == 0)
    {
        payload.Emitted.r += sum;
    }
    else if (colorChannel == 1)
    {
        payload.Emitted.g += sum;
    }
    else
    {
        payload.Emitted.b += sum;
    }
}

[shader("miss")]
void Main(inout Payload payload)
{
    #ifdef ENABLE_ATMOSPHERE
    {
        ScatterInAtmosphere(payload);
    }
    #else
    {
        float4 colorPdf;
        #ifdef SHOW_ENV_MAP_DIRECTLY
        {
            uint2 textureSize;
            uint mipLevels;
            uEnvMapTexture.GetDimensions(0, textureSize.x, textureSize.y, mipLevels);

            float3 direction = payload.Direction;

            // Rotate the direction with altitude and azimuth
            float azimuth = uUBO.EnvMapRotationAzimuth / 180.0f * M_PI; // Convert degrees to radians
            float altitude = uUBO.EnvMapRotationAltitude / 180.0f * M_PI; // Convert degrees to radians
            float3 rotatedDirection = Rotate(direction, float3(0.0f, 1.0f, 0.0f), -azimuth);
            rotatedDirection = Rotate(rotatedDirection, float3(1.0f, 0.0f, 0.0f), -altitude);

            colorPdf = uEnvMapTexture.SampleLevel(uSampler, DirectionToUV(rotatedDirection), 0);
        }
        #else // Show env map only indirectly (bounced from object)
        {
            if (payload.Depth > 0)
            {
                uint2 textureSize;
                uint mipLevels;
                uEnvMapTexture.GetDimensions(0, textureSize.x, textureSize.y, mipLevels);

                float3 direction = payload.Direction;

                // Rotate the direction with altitude and azimuth
                float azimuth = uUBO.EnvMapRotationAzimuth / 180.0f * M_PI; // Convert degrees to radians
                float altitude = uUBO.EnvMapRotationAltitude / 180.0f * M_PI; // Convert degrees to radians
                float3 rotatedDirection = Rotate(direction, float3(0.0f, 1.0f, 0.0f), -azimuth);
                rotatedDirection = Rotate(rotatedDirection, float3(1.0f, 0.0f, 0.0f), -altitude);

                colorPdf = uEnvMapTexture.SampleLevel(uSampler, DirectionToUV(rotatedDirection), 0);
            }
            else
            {
                colorPdf = float4(0.0f, 0.0f, 0.0f, 1.0f);
            }
        }
        #endif

        payload.Emitted = colorPdf.rgb * uUBO.EnvironmentIntensity;

        #ifdef FURNACE_TEST_MODE
        {
            payload.Emitted = float3(1.0f);
        }
        #endif

        #ifdef ENABLE_ENV_MAP_MIS
        if (payload.Depth > 0)
        {
            payload.Emitted *= PowerHeuristics(payload.PDF, colorPdf.a);
        }
        #endif
    }
    #endif

    payload.Depth = MAX_DEPTH; // Set depth to max value to indicate no hit
}