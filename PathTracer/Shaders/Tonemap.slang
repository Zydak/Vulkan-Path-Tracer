[[vk::image_format("rgba32f")]]
[[vk::binding(0, 0)]] RWTexture2D<float4> uInputImage;

[[vk::image_format("rgba8")]]
[[vk::binding(1, 0)]] RWTexture2D<float4> uOutputImage;

struct UniformBuffer
{
    float Exposure;
    float Gamma;
}

[[vk::binding(2, 0)]] ConstantBuffer<UniformBuffer> uUBO;

[shader("compute")]
[numthreads(32,32,1)]
void Main(uint3 threadID : SV_DispatchThreadID)
{
    float4 color = uInputImage.Load(threadID.xy);
    color = pow(color, float4(1.0f / uUBO.Gamma));
    uOutputImage.Store(threadID.xy, color);
}