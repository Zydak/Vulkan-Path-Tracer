[[vk::image_format("rgba32f")]]
[[vk::binding(0, 0)]] Texture2D<float4> uInputImage;

[[vk::image_format("rgba8")]]
[[vk::binding(1, 0)]] RWTexture2D<float4> uOutputImage;

[[vk::image_format("rgba32f")]]
[[vk::binding(3, 0)]] Texture2D<float4> uBloomImage;

struct UniformBuffer
{
    float Exposure;
    float Gamma;
}

[[vk::binding(2, 0)]] ConstantBuffer<UniformBuffer> uUBO;

static const float3x3 ACESInputMat =
{
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat =
{
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 ACESFitted(float3 color)
{
    color = mul(ACESInputMat, color);

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = mul(ACESOutputMat, color);

    // Clamp to [0, 1]
    color = saturate(color);

    return color;
}

[shader("compute")]
[numthreads(8,8,1)]
void Main(uint3 threadID : SV_DispatchThreadID)
{
    uint2 size;
    uOutputImage.GetDimensions(size.x, size.y);
    if (threadID.x >= size.x || threadID.y >= size.y)
        return;
    
    float3 color = uInputImage.Load(int3(threadID.xy, 0)).rgb;
    color += uBloomImage.Load(int3(threadID.xy, 0)).rgb;
    
    color *= uUBO.Exposure;
    color = pow(color, float3(1.0 / uUBO.Gamma));
    color = ACESFitted(color);
    uOutputImage.Store(threadID.xy, float4(color, 1.0));
}