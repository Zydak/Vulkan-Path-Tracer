struct PushConstants 
{
    float4x4 pModelViewProj;
};

[[vk::push_constant]] PushConstants push;

struct VertexInput 
{
    float3 Position;
    float3 Normal;
    float2 TexCoord;
};

struct VertexOutput 
{
    float4 Position : SV_POSITION;
    float2 TexCoord;
};

[shader("vertex")]
VertexOutput main(VertexInput input) 
{
    VertexOutput output;

    output.Position = mul(push.pModelViewProj, float4(input.Position, 1.0f));
    output.TexCoord = input.TexCoord;
    
    return output;
}