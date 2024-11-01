import rtcommon;
import Camera;
import Ray;
import Film;
import Payload;
import Sampler;

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

    RTPayload payload = RTPayload();
    payload.Sampler = Sampler(LaunchID.y + LaunchSize.x * LaunchID.x + getRealtimeClock().x);

    float3 finalValue = 0.0f;
    for (int i = 0; i < push.SamplesPerFrame; i++)
    {
        float3 weight = 1.0f;
        float3 hitValue = 0.0f;

        payload.HitValue = float3(0.0f);
        payload.Weight = float3(1.0f);
        payload.Depth = 0;

        Ray ray = camera.GetRay(inUV * 2.0 - 1.0);

        for (; payload.Depth < push.MaxDepth; payload.Depth++)
        {
            ray.Trace(uTopLevelAS, RAY_FLAG_FORCE_OPAQUE, payload);

            weight *= payload.Weight;
            hitValue += payload.HitValue * weight;

            // Russian Roulette

            float p = max(weight.x, max(weight.y, weight.z));

            // Because of energy compensation weight is sometimes greater than 1
            // and p gets compared to random variable that is between 0 and 1
            // it also has to be between 0 and 1 hence the min()
            p = min(p, 1.0f);
            if (p <= payload.Sampler.UniformFloat())
                break;
            weight /= p;
        }

        finalValue += hitValue;
    }
    finalValue /= push.SamplesPerFrame;

    film.WritePixel(LaunchID.xy, float4(finalValue, 1.0f), push.Frame);
}