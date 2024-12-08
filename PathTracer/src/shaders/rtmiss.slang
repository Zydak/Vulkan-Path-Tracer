import rtcommon;
import Payload;

struct Input
{
    [[vk::location(0)]] RTPayload Payload;
}

[[vk::binding(1, 1)]] Sampler2D uEnvironmentMap;

[[vk::push_constant]] PushConstant push;

[shader("miss")]
void main(inout Input input)
{
    float3 rayDir = input.Payload.RayDirection;
    float3 color;

    // To rotate the environment map the direction used to sample it has to be rotated
    rayDir = Rotate(rayDir, float3(1, 0, 0), M_PI_2);
    rayDir = Rotate(rayDir, float3(0, 0, 1), M_PI / 2.5f);
    
    rayDir = Rotate(rayDir, float3(0, 0, 1), push.EnvAzimuth);
    rayDir = Rotate(rayDir, float3(0, 1, 0), push.EnvAltitude);

    uint2 textureSize;
    uint mipLevels;
    uEnvironmentMap.GetDimensions(0, textureSize.x, textureSize.y, mipLevels);

    // Sample the environment map
    float2 sphericalCoords = DirectionToSpherical(rayDir);
    float2 texCoords = SphericalToTexCoords(sphericalCoords.x, sphericalCoords.y);
    color = uEnvironmentMap.SampleLevel( { texCoords.x, texCoords.y }, 0).rgb;

#ifdef SHOW_SKYBOX
    input.Payload.HitValue = color;
#else
    if (input.Payload.Depth == 0)
        input.Payload.HitValue = float3(0.0f);
    else
        input.Payload.HitValue = color;
#endif

    // Terminate the path
    input.Payload.Depth = DEPTH_INFINITE;

    // GBuffer colors
    input.Payload.GBufferColor = color;
    input.Payload.GBufferNormal = float3(0.0f);
}