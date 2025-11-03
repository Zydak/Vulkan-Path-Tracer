[[vk::image_format("rgba32f")]]
[[vk::binding(0, 0)]] Texture2D<float4> uInputImage;

[[vk::image_format("rgba8")]]
[[vk::binding(1, 0)]] RWTexture2D<float4> uOutputImage;

[[vk::image_format("rgba32f")]]
[[vk::binding(3, 0)]] Texture2D<float4> uBloomImage;

[[vk::binding(4, 0)]] SamplerState uSampler;

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

// AGX: https://iolite-engine.com/blog_posts/minimal_agx_implementation
// 0: Default, 1: Golden, 2: Punchy
#define AGX_LOOK 0

// Mean error^2: 3.6705141e-06
float3 agxDefaultContrastApprox(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    float3 x6 = x4 * x2;
  
    return - 17.86     * x6 * x
         + 78.01     * x6
         - 126.7     * x4 * x
         + 92.06     * x4
         - 28.72     * x2 * x
         + 4.361     * x2
         - 0.1718    * x
         + 0.002857;
}


float3 agx(float3 val)
{
    const float3x3 agx_mat = float3x3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992,  0.878468636469772,  0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104
    );
    
    const float min_ev = -12.47393f;
    const float max_ev = 4.026069f;

    // Input transform (inset)
    val = mul(val, agx_mat);

    // Log2 space encoding
    val = clamp(log2(val), min_ev, max_ev);
    val = (val - min_ev) / (max_ev - min_ev);
  
    // Apply sigmoid function approximation
    val = agxDefaultContrastApprox(val);

    return val;
}

float3 agxEotf(float3 val)
{
    const float3x3 agx_mat_inv = float3x3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116
    );
    
    // Inverse input transform (outset)
    val = mul(val, agx_mat_inv);

    // sRGB IEC 61966-2-1 2.2 Exponent Reference EOTF Display
    // NOTE: We're linearizing the output here. Comment/adjust when
    // *not* using a sRGB render target
    val = pow(val, float3(2.2));

    return val;
}

float3 agxLook(float3 val)
{  
    // Default
    float3 offset = float3(0.0);
    float3 slope = float3(1.0);
    float3 power = float3(1.0);
    float sat = 1.0;
 
#if AGX_LOOK == 1
    // Golden
    slope = float3(1.0, 0.9, 0.5);
    power = float3(0.8);
    sat = 0.8;
#elif AGX_LOOK == 2
    // Punchy
    slope = float3(1.0);
    power = float3(1.35, 1.35, 1.35);
    sat = 1.4;
#endif
  
    // ASC CDL
    val = pow(val * slope + offset, power);

    const float3 lw = float3(0.2126, 0.7152, 0.0722);
    float luma = dot(val, lw);

    return luma + sat * (val - luma);
}

float3 AGXTonemap(float3 color)
{
    color = agx(color);
    color = agxLook(color);
    color = agxEotf(color);
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
    color += uBloomImage.SampleLevel(uSampler, float2(threadID.xy) / float2(size), 0).rgb;

    color *= uUBO.Exposure;
    color = pow(color, float3(1.0 / uUBO.Gamma));
    
    color = ACESFitted(color);
    uOutputImage.Store(threadID.xy, float4(color, 1.0));
}