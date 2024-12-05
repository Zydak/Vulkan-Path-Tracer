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
    rayDir = Rotate(rayDir, float3(1, 0, 0), M_PI_2);

    rayDir = Rotate(rayDir, float3(0, 0, 1), push.EnvAzimuth);
    rayDir = Rotate(rayDir, float3(1, 0, 0), push.EnvAltitude);

    uint2 textureSize;
    uint mipLevels;
    uEnvironmentMap.GetDimensions(0, textureSize.x, textureSize.y, mipLevels);

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
    input.Payload.Depth = DEPTH_INFINITE;

    input.Payload.GBufferColor = color;
    input.Payload.GBufferNormal = float3(0.0f);
}