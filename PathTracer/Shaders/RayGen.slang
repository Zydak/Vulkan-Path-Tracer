import RTCommon;
import Sampler;

[[vk::image_format("rgba32f")]]
[[vk::binding(0, 0)]] RWTexture2D<float4> uImage;

[[vk::binding(1, 0)]] RaytracingAccelerationStructure uTopLevelAS;

struct UniformBuffer
{
    float4x4 ViewInverse;
    float4x4 ProjectionInverse;
    uint FrameCount;
    uint Seed;
};

[[vk::binding(2, 0)]] ConstantBuffer<UniformBuffer> uUBO;

[shader("raygeneration")]
void Main()
{
    uint3 size = DispatchRaysDimensions();
    uint3 LaunchID = DispatchRaysIndex();

    float3 prevColor = uImage[LaunchID.xy].rgb;

    const float2 pixelCenter = float2(LaunchID.xy) + float2(0.5f);
    const float2 pixelUV = pixelCenter / float2(size.xy);
    const float2 d = pixelUV * 2.0f - 1.0f;

    float4 origin       = mul(uUBO.ViewInverse, float4(0, 0, 0, 1));
    float4 target       = mul(uUBO.ProjectionInverse, float4(d.x, d.y, 1.0f, 1.0f));
    float4 direction    = mul(uUBO.ViewInverse, float4(normalize(target.xyz), 0.0f));

    Payload payload;
    payload.Depth = 0;
    payload.Origin = origin.xyz;
    payload.Direction = direction.xyz;
    payload.SurfaceColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    payload.LightColor = float4(0.0f, 0.0f, 0.0f, 1.0f);
    payload.Sampler = Sampler(LaunchID.y + size.x * LaunchID.x + uUBO.Seed);

    float3 weight = 1.0f;
    float3 hitValue = 0.0f;
    // Depth is incremented in closest hit and miss shaders
    for (;payload.Depth < 20;)
    {
        RayDesc rayDesc;
        rayDesc.Origin = payload.Origin;
        rayDesc.Direction = payload.Direction;
        rayDesc.TMin = 0.00001f;
        rayDesc.TMax = 1000.0f;

        TraceRay(uTopLevelAS, RAY_FLAG_FORCE_OPAQUE, 0xff, 0u, 0u, 0u, rayDesc, payload);

        weight *= payload.SurfaceColor.rgb;
        hitValue += payload.LightColor.rgb * weight;
    }

    float3 sampleColor = hitValue;

    float a = 1.0f / float(uUBO.FrameCount + 1);
    float3 color = lerp(prevColor, sampleColor, a);

    if (!isinf(color.x) && !isinf(color.y) && !isinf(color.z) && !isnan(color.x) && !isnan(color.y) && !isnan(color.z))
    {
        uImage[LaunchID.xy] = float4(color, 1.0f);
    }
}