import Sampler;

public struct Payload
{
    public float3 Origin;
    public float3 Direction;
    public float4 SurfaceColor;
    public float4 LightColor;

    public uint Depth;
    public Sampler Sampler;
};

public struct Vertex
{
    public float3 Position;
    public float3 Normal;
    public float2 TexCoord;
};