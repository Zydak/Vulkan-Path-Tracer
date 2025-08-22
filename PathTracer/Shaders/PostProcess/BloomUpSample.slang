
[[vk::image_format("rgba32f")]]
[[vk::binding(0, 0)]] RWTexture2D<float4> uOutputImage;

[[vk::image_format("rgba32f")]]
[[vk::binding(1, 0)]] RWTexture2D<float4> uInputImage;

[[vk::binding(2, 0)]] SamplerState uSampler;

struct PushConstant
{
    float BloomThreshold;
    float BloomStrength;
    int FirstDispatch;
}

[vk::push_constant] PushConstant pPush;

[shader("compute")]
[numthreads(8,8,1)]
void Main(uint3 threadID : SV_DispatchThreadID)
{
    uint2 outputTextureSize;
    uOutputImage.GetDimensions(outputTextureSize.x, outputTextureSize.y);
    if (threadID.x >= outputTextureSize.x || threadID.y >= outputTextureSize.y)
        return;

    uint2 inputTextureSize;
    uInputImage.GetDimensions(inputTextureSize.x, inputTextureSize.y);

    // This could be improved with gaussian blur instead
    float3 color = 0.0f;
    int range = 2;
    for (int i = -range; i < range; i++)
    {
        for (int j = -range; j < range; j++)
        {
            int2 samplePos = int2(threadID.xy / 2) + int2(i, j) + 1;
            float2 UV = (float2(samplePos) + 0.5f) / float2(inputTextureSize);
            samplePos = clamp(samplePos, int2(0, 0), int2(inputTextureSize.x - 1, inputTextureSize.y - 1));
            color += uInputImage.Load(int2(samplePos)).rgb;
        }
    }
    color /= pow((range * 2 + 1), 2);
    color *= pPush.BloomStrength;

    float3 currentColor = uOutputImage.Load(threadID.xy).rgb;
    uOutputImage.Store(threadID.xy, float4(color + currentColor, 1.0));    
}