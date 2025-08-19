import RTCommon;
import Sampler;

[[vk::image_format("rgba32f")]]
[[vk::binding(0, 0)]] RWTexture2D<float4> uImage;

[[vk::binding(1, 0)]] RaytracingAccelerationStructure uTopLevelAS;

[[vk::binding(2, 0)]] ConstantBuffer<UniformBuffer> uUBO;

[shader("raygeneration")]
void Main()
{
    uint3 size = DispatchRaysDimensions();
    uint3 LaunchID = DispatchRaysIndex();

    Payload payload;
    payload.Sampler = Sampler(LaunchID.y + size.x * LaunchID.x + uUBO.Seed);

    float3 prevColor = uImage[LaunchID.xy].rgb;

    float3 accumulatedRadiance = 0.0f;
    for (uint i = 0; i < uUBO.SampleCount; i++)
    {
        const float2 pixelCenter = float2(LaunchID.xy) + float2(0.5f) + payload.Sampler.UniformFloat2(-0.5f, 0.5f); // Add small jitter for anti aliasing
        const float2 pixelUV = pixelCenter / float2(size.xy);
        float2 d = pixelUV * 2.0f - 1.0f;

        float3 origin       = mul(uUBO.ViewInverse, float4(0, 0, 0, 1)).xyz;
        float3 target       = mul(uUBO.ProjectionInverse, float4(d.x, d.y, 1.0f, 1.0f)).xyz;
        float3 direction    = mul(uUBO.ViewInverse, float4(normalize(target), 0.0f)).xyz;

        float3 focusPoint = origin + direction * max(uUBO.FocusDistance, 0.001f);
        float2 randomOffset = payload.Sampler.UniformFloat2(-0.5f, 0.5f) * uUBO.DepthOfFieldStrength;

        const float3 cameraRight = float3(uUBO.ViewInverse[0][0], uUBO.ViewInverse[1][0], uUBO.ViewInverse[2][0]);
        const float3 cameraUp = float3(uUBO.ViewInverse[0][1], uUBO.ViewInverse[1][1], uUBO.ViewInverse[2][1]);
        origin += randomOffset.x * cameraRight + randomOffset.y * cameraUp;

        direction = normalize(focusPoint - origin);

        payload.Depth = 0;
        payload.Origin = origin;
        payload.Direction = direction;
        payload.Contribution = float3(1.0f, 1.0f, 1.0f);
        payload.Emitted = float3(0.0f, 0.0f, 0.0f);
        payload.InMedium = false;

        float3 pathThroughput = 1.0f;
        float3 radiance = 0.0f;
        // Depth is incremented in closest hit and miss shaders
        for (;payload.Depth < uUBO.MaxDepth;)
        {
            RayDesc rayDesc;
            rayDesc.Origin = payload.Origin;
            rayDesc.Direction = normalize(payload.Direction);
            rayDesc.TMin = 0.00001f;
            rayDesc.TMax = 1000.0f;

            payload.Contribution = float3(1.0f, 1.0f, 1.0f);
            payload.Emitted = float3(0.0f, 0.0f, 0.0f);
            TraceRay(uTopLevelAS, RAY_FLAG_FORCE_OPAQUE, 0xff, 0u, 0u, 0u, rayDesc, payload);

            radiance += payload.Emitted * pathThroughput;
            pathThroughput *= payload.Contribution;

            // NOT WORKING!
            //
            // // Russian roulette
            // float p = max(pathThroughput.x, max(pathThroughput.y, pathThroughput.z));
            // // Because of energy compensation pathThroughput is sometimes greater than 1
            // // and p gets compared to random variable that is between 0 and 1
            // // so it also has to be between 0 and 1 hence the min()
            // p = min(p, 1.0f);
            // if (p <= payload.Sampler.UniformFloat())
            //     break;
            // pathThroughput /= p;
        }

        accumulatedRadiance += radiance;
    }
    accumulatedRadiance /= (float)uUBO.SampleCount;
    
    // Eliminate high luminance
    float lum = dot(accumulatedRadiance, float3(0.212671f, 0.715160f, 0.072169f));
    float scale = uUBO.MaxLuminance / max(lum, uUBO.MaxLuminance);
    accumulatedRadiance *= scale;

    float3 color;
    if (uUBO.FrameCount > 1)
    {
        float a = 1.0f / float(uUBO.FrameCount);
        color = lerp(prevColor, accumulatedRadiance, a);
    }
    else
    {
        color = accumulatedRadiance;
    }

    // I'm not exactly sure where, but a NAN happens from time to time. It's somewhere in the SampleGlass function
    // but I can't pinpoint it. So just get rid of nans if there are any.
    if (!isinf(color.x) && !isinf(color.y) && !isinf(color.z) && !isnan(color.x) && !isnan(color.y) && !isnan(color.z))
    {
        uImage[LaunchID.xy] = float4(color, 1.0f);
    }
}