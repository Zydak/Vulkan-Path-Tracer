import Surface;
import Sampler;
import RTCommon;

public struct BxDFSample
{
    public float3 L;
    public float3 BxDF;
    public float PDF;
}

public struct BxDFEval
{
    public float3 BxDF;
    public float PDF;
}

// Material data that's passed in from the CPU
public struct CPUMaterial
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

public struct Material
{
    public CPUMaterial Properties;

    public float Eta;
    public float Ax;
    public float Ay;

    [mutating]
    public void Initialize(
        in CPUMaterial materialData,
        in Surface surface,
        in SamplerState textureSampler,
        Texture2D baseColorTexture,
        Texture2D roughnessTexture,
        Texture2D metallicTexture,
        Texture2D emissiveTexture
    )
    {
        Properties = materialData;
        const float d = (float)(dot(WorldRayDirection(), surface.GetNormal()) < 0.0);
        Eta = d * (1.0 / Properties.IOR) + (1.0 - d) * Properties.IOR;

        const float aspect = sqrt(1.0 - Properties.Anisotropy * 0.9);
        Ax = max(0.001, Properties.Roughness / aspect);
        Ay = max(0.001, Properties.Roughness * aspect);

        // Textures
        Properties.BaseColor *= baseColorTexture.SampleLevel(textureSampler, surface.GetTextureCoordinates(), 0).rgb;
        Properties.Roughness *= roughnessTexture.SampleLevel(textureSampler, surface.GetTextureCoordinates(), 0).r;
        Properties.Metallic *= metallicTexture.SampleLevel(textureSampler, surface.GetTextureCoordinates(), 0).r;
        Properties.EmissiveColor *= emissiveTexture.SampleLevel(textureSampler, surface.GetTextureCoordinates(), 0).rgb;
    }

    // BxDF Functions
    public BxDFSample SampleDiffuse(inout Sampler sampler, in float3 V)
    {
        float3 L = sampler.RandomSphereVecCosineWeight(float3(0.0f, 0.0f, 1.0f)); // Tangent space

        BxDFEval eval = EvaluateDiffuse(V, L);

        return { L, eval.BxDF, eval.PDF };
    }

    public BxDFEval EvaluateDiffuse(in float3 V, in float3 L)
    {
        float PDF = L.z * M_1_OVER_PI;
        float3 BRDF = M_1_OVER_PI * Properties.BaseColor * L.z;

        return { BRDF, PDF };
    }
}