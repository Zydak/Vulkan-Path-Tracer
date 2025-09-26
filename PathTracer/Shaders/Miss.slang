import RTCommon;
import Defines;
import Sampler;
import Atmosphere;

import Bindings;

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
    float3 SUN_DIRECTION = Rotate(float3(0.0f, 0.0f, -1.0f), float3(1.0f, 0.0f, 0.0f), altitude); // Start from -Z (front)
    SUN_DIRECTION = Rotate(SUN_DIRECTION, float3(0.0f, 1.0f, 0.0f), azimuth);

    float sum = 0.0f;
    float throughput = 1.0f;
    for (int i = 0; i < 100; i++)
    {
        int componentHit;
        float deltaT = SampleAtmosphereScatterDistance(payload.Sampler, payload.Origin, payload.Direction, colorChannel, componentHit);
        if (deltaT <= 0.0f)
            break; // Exited the atmosphere

        payload.Origin += deltaT * payload.Direction;

        float3 sunDirSampled;
        float4 colorPdf;
        payload.Sampler.SampleSunDisk(SUN_DIRECTION, 0.004675f, sunDirSampled, colorPdf);
        colorPdf.rgb *= uUBO.EnvironmentIntensity;

        // Compute transmittance to the sun
        float3 transmittanceToSun = CalculateTransmittanceThroughAtmosphere(payload.Sampler, payload.Origin, sunDirSampled, colorChannel);

        // Stochastically choose between Rayleigh and Mie
        float3 newDir;
        if (componentHit == 0)
        {
            // Rayleigh
            newDir = payload.Sampler.SampleRayleigh(payload.Direction);
            payload.Emitted += throughput * RayleighPhase(payload.Direction, sunDirSampled) * transmittanceToSun * (colorPdf.r / colorPdf.a);

            // Rayleigh is perfectly importance sampled and has no absorption so throughput is not changed
            // throughput *= RayleighPhase(dot(payload.Direction, newDir)) / RayleighPhase(dot(payload.Direction, newDir));
        }
        else
        {
            // Mie
            newDir = payload.Sampler.SampleHenyeyGreenstein(payload.Direction, 0.85f); // Sample henyey since it's a pretty close match
            payload.Emitted += throughput * PhaseMie(payload.Direction, sunDirSampled) * transmittanceToSun * (colorPdf.r / colorPdf.a);

            // Mie is no longer perfectly importance sampled, MIE phase function was used but the sampling was done with Henyey-Greenstein
            throughput *= PhaseMie(payload.Direction, newDir) / PhaseHenyeyGreenstein(payload.Direction, newDir, 0.85f);
        }

        payload.Direction = newDir;
    }
}

[shader("miss")]
void Main(inout Payload payload)
{
    #ifdef ENABLE_ATMOSPHERE
    {
        
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
            float3 rotatedDirection = Rotate(direction, float3(1.0f, 0.0f, 0.0f), -altitude);
            rotatedDirection = Rotate(rotatedDirection, float3(0.0f, 1.0f, 0.0f), -azimuth);

            colorPdf = uEnvMapTexture.SampleLevel(uTextureSampler, DirectionToUV(rotatedDirection), 0);
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
                float3 rotatedDirection = Rotate(direction, float3(1.0f, 0.0f, 0.0f), -altitude);
                rotatedDirection = Rotate(rotatedDirection, float3(0.0f, 1.0f, 0.0f), -azimuth);

                colorPdf = uEnvMapTexture.SampleLevel(uTextureSampler, DirectionToUV(rotatedDirection), 0);
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