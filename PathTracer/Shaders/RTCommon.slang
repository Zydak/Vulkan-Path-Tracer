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

public struct Material
{
    public float3 BaseColor;
    public float3 EmissiveColor;
    public float Metallic;
    public float Roughness;
    public float IOR;
    public float Transmission;
    public float Anisotropy;
    public float AnisotropyRotation;
}