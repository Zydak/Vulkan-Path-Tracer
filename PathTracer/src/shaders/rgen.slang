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
[[vk::binding(8, 0)]] RWStructuredBuffer<float> uDof;

[[vk::push_constant]] PushConstant push;

[shader("raygeneration")]
void main()
{
    Film film = Film(uImage);
    uint3 LaunchID = DispatchRaysIndex();
    uint3 LaunchSize = DispatchRaysDimensions();

    if (LaunchID.x == (LaunchSize.x / 2) && LaunchID.y == (LaunchSize.y / 2))
    {
        const float2 pixelCenter = float2(LaunchID.xy) + float2(0.5);
        const float2 inUV = pixelCenter / float2(LaunchSize.xy);
        float2 d = inUV * 2.0 - 1.0;

        float4 origin = mul(float4(0, 0, 0, 1), uUniformBuffer.ViewInverse);
        float4 target = mul(float4(d.x, d.y, 1, 1), uUniformBuffer.ProjInverse);
        float3 direction = mul(float4(normalize(target.xyz), 0.0f), uUniformBuffer.ViewInverse).xyz;

        RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
        query.__rayQueryInitializeEXT(uTopLevelAS, RAY_FLAG_FORCE_OPAQUE, 0xFF, origin.xyz, 0.0001, direction, 1000000);
        query.Proceed();
        if (query.CommittedStatus() != COMMITTED_NOTHING)
        {
            uDof[0] = query.CommittedRayT();
        }
    }

    Camera camera = Camera(uUniformBuffer.ViewInverse, uUniformBuffer.ProjInverse);

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
        payload.InMedium = false;

        // Random float2 from -0.5 to 0.5
        const float2 antiAliasingJitter = (payload.Sampler.UniformFloat2() - 0.5f) * push.AAstrength;

        const float2 pixelCenter = float2(LaunchID.xy) + float2(0.5, 0.5) + antiAliasingJitter;
        const float2 inUV = pixelCenter / float2(LaunchSize.xy);

        Ray ray = camera.GetRay(push.FocalLenght, push.DoFStrenght, payload.Sampler, inUV * 2.0 - 1.0);

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

        // Eliminate caustics
#ifdef USE_CAUSTICS_SUPPRESION
        const float maxLuminance = push.CausticsLuminance;
        float lum = dot(hitValue, float3(0.212671f, 0.715160f, 0.072169f));
        if (lum > maxLuminance)
        {
            hitValue *= maxLuminance / lum;
        }
#endif

        finalValue += hitValue;
    }
    finalValue /= push.SamplesPerFrame;

    film.WritePixel(LaunchID.xy, float4(finalValue, 1.0f), push.Frame);
}