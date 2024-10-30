import rtcommon;

struct Input
{
    [[vk::location(0)]] RtPayload Payload;
}

[[vk::binding(1, 1)]] Sampler2D uEnvironmentMap;

[[vk::push_constant]] PushConstant push;

[require(spvImageQuery)]
[require(spvSparseResidency)]
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

    int2 uv = int2(texCoords * float2(textureSize));

    color = uEnvironmentMap.Load( { uv.x, uv.y, 0 }).rgb;

    input.Payload.HitValue = color;
    input.Payload.Depth = DEPTH_INFINITE;
    input.Payload.SurfaceAlbedo = color;
    input.Payload.SurfaceNormal = float3(0.0f);
}