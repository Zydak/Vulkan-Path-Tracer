import rtcommon;
import Camera;
import Ray;
import Film;

[[vk::binding(0, 0)]] RaytracingAccelerationStructure uTopLevelAS;

[[vk::image_format("rgba32f")]]
[[vk::binding(1, 0)]] RWTexture2D<float4> uImage;
[[vk::binding(0, 1)]] ConstantBuffer<GlobalUniform> uUniformBuffer;

[[vk::push_constant]] PushConstant push;

[shader("raygeneration")]
void main()
{
    Film film = Film(uImage);
    uint3 LaunchID = DispatchRaysIndex();
    uint3 LaunchSize = DispatchRaysDimensions();

    Camera camera = Camera(uUniformBuffer.ViewInverse, uUniformBuffer.ProjInverse);

    const float2 pixelCenter = float2(LaunchID.xy) + float2(0.5, 0.5);
    const float2 inUV = pixelCenter / float2(LaunchSize.xy);

    Ray ray = camera.GetRay(inUV * 2.0 - 1.0);

    RtPayload payload;
    payload.HitValue = float3(0.0f);
    payload.Seed = LaunchID.y + LaunchSize.x * LaunchID.x + getRealtimeClock().x;

    ray.Trace(uTopLevelAS, RAY_FLAG_FORCE_OPAQUE, payload);

    float3 finalHitValue = payload.HitValue;

    film.WritePixel(LaunchID.xy, float4(finalHitValue, 1.0f), push.Frame);
}