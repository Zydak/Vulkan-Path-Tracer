import RTCommon;
import Defines;
import Sampler;
import Atmosphere;

import Bindings;

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
            float azimuth = uUBO.SkyRotationAzimuth / 180.0f * M_PI; // Convert degrees to radians
            float altitude = uUBO.SkyRotationAltitude / 180.0f * M_PI; // Convert degrees to radians
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
                float azimuth = uUBO.SkyRotationAzimuth / 180.0f * M_PI; // Convert degrees to radians
                float altitude = uUBO.SkyRotationAltitude / 180.0f * M_PI; // Convert degrees to radians
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

        #ifdef ENABLE_SKY_MIS
        if (payload.Depth > 0)
        {
            payload.Emitted *= PowerHeuristics(payload.PDF, colorPdf.a);
        }
        #endif
    }
    #endif

    payload.Depth = MAX_DEPTH; // Set depth to max value to indicate no hit
}