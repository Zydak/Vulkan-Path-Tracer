
[[vk::image_format("rgba32f")]]
[[vk::binding(0, 0)]] RWTexture2D<float4> uInputImage;

[[vk::image_format("rgba32f")]]
[[vk::binding(1, 0)]] RWTexture2D<float4> uOutputImage;

[[vk::binding(2, 0)]] SamplerState uSampler;

struct PushConstant
{
    float BloomThreshold;
    float BloomStrength;
    int FirstDispatch;
    float FallofRange;
}

[vk::push_constant] PushConstant pPush;

[shader("compute")]
[numthreads(8,8,1)]
void Main(uint3 threadID : SV_DispatchThreadID)
{
    uint2 inputTextureSize;
    uInputImage.GetDimensions(inputTextureSize.x, inputTextureSize.y);
    if (threadID.x >= inputTextureSize.x || threadID.y >= inputTextureSize.y)
        return;

    uint2 outputTextureSize;
    uOutputImage.GetDimensions(outputTextureSize.x, outputTextureSize.y);
    
    if (pPush.FirstDispatch > 0) // Separate color values
    {
        float3 color = uInputImage.Load(threadID.xy).rgb;
        const float brightness = dot(color, float3(0.2126, 0.7152, 0.0722));
        
        // Gradual falloff instead of binary cutoff
        // Using smoothstep for a smooth transition around the threshold
        float falloffRange = pPush.FallofRange; // Adjust this to control falloff width
        float falloffStart = pPush.BloomThreshold - falloffRange;
        float falloffEnd = pPush.BloomThreshold + falloffRange;
        float factor = smoothstep(falloffStart, falloffEnd, brightness);
        
        color *= factor;

        uOutputImage.Store(threadID.xy, float4(color, 1.0));
    }
    else // Down sample
    {
        // This could be improved with gaussian blur instead
        float3 color = 0.0f;
        int range = 2;
        for (int i = -range; i < range; i++)
        {
            for (int j = -range; j < range; j++)
            {
                int2 samplePos = int2(threadID.xy * 2) + int2(i, j);
                samplePos = clamp(samplePos, int2(0, 0), int2(inputTextureSize.x - 1, inputTextureSize.y - 1));
                color += uInputImage.Load(int2(samplePos)).rgb;
            }
        }
        color /= pow((range * 2 + 1), 2);
        color *= pPush.BloomStrength;

        uOutputImage.Store(threadID.xy, float4(color, 1.0));
    }
}