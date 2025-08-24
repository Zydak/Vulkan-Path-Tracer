import RTCommon;
import Defines;

[[vk::binding(2, 0)]] ConstantBuffer<UniformBuffer> uUBO;

[[vk::binding(10, 0)]] SamplerState uSampler;

[[vk::image_format("rgba32f")]]
[[vk::binding(15, 0)]] Texture2D uEnvMapTexture;

float2 getSphericalUv(float3 v)
{
    float gamma = asin(-v.y);
    float theta = atan2(v.z, v.x);

    float2 uv = float2(theta * M_1_OVER_PI * 0.5F, gamma * M_1_OVER_PI) + 0.5F;
    return uv;
}

[shader("miss")]
void Main(inout Payload payload)
{
    float3 color;
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

        color = uEnvMapTexture.SampleLevel(uSampler, getSphericalUv(rotatedDirection), 0).rgb;
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

            color = uEnvMapTexture.SampleLevel(uSampler, getSphericalUv(rotatedDirection), 0).rgb;
        }
        else
        {
            color = float3(0.0f, 0.0f, 0.0f);
        }
    }
    #endif

    payload.Emitted = color;
    payload.Depth = MAX_DEPTH; // Set depth to max value to indicate no hit
}