import Sampler;

public struct Payload
{
    public float3 Origin;
    public float3 Direction;
    public float3 SurfaceColor;
    public float3 LightColor;

    public uint Depth;
    public Sampler Sampler;

    // For tracking the medium the ray is in
    public bool InMedium = false;
    public float MediumDensity = 0.0;
    public float MediumAnisotropy = 1.0;
    public float3 MediumColor = float3(1.0);
    public float3 MediumEmissiveColor = float3(0.0);
};

public struct Vertex
{
    public float3 Position;
    public float3 Normal;
    public float2 TexCoord;
};