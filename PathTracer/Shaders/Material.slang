import Surface;
import Sampler;
import RTCommon;
import Defines;

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
    public float3 MediumColor;
    public float3 MediumEmissiveColor;
    public float Metallic;
    public float Roughness;
    public float IOR;
    public float Transmission;
    public float Anisotropy;
    public float AnisotropyRotation;

    public float MediumDensity;
    public float MediumAnisotropy;
}

public struct Material
{
    public CPUMaterial Properties;

    public float Eta;
    public float Ax;
    public float Ay;
    Texture2DArray m_ReflectionLookupTable;
    Texture2DArray m_RefractionLookupTableHitFromOutside;
    Texture2DArray m_RefractionLookupTableHitFromInside;
    SamplerState m_Sampler;

    [mutating]
    public void Initialize(
        in CPUMaterial materialData,
        in Surface surface,
        in SamplerState textureSampler,
        in Texture2D baseColorTexture,
        in Texture2D roughnessTexture,
        in Texture2D metallicTexture,
        in Texture2D emissiveTexture,
        in Texture2DArray lookupTableReflection,
        in Texture2DArray lookupTableRefractionHitFromOutside,
        in Texture2DArray lookupTableRefractionHitFromInside
    )
    {
        Properties = materialData;

        Properties.Roughness *= Properties.Roughness;

        Properties.IOR = max(Properties.IOR, 1.0001f);

        // Eta is relative index of refraction, so ETA = IORin / IORout. If we're in air (IOR 1) and we refract into glass (IOR 1.5)
        // ETA = 1 / 1.5 = 0.6667
        if (surface.WasHitFromInside())
        {
            Eta = Properties.IOR;
        }
        else
        {
            Eta = 1.0f / Properties.IOR;
        }

        const float aspect = sqrt(1.0 - sqrt(Properties.Anisotropy) * 0.9);
        Ax = max(0.001, Properties.Roughness / aspect);
        Ay = max(0.001, Properties.Roughness * aspect);

        //Properties.Metallic = min(Properties.Metallic, 0.999);
        
        // Textures

        Properties.BaseColor *= baseColorTexture.SampleLevel(textureSampler, surface.GetTextureCoordinates(), 0).rgb;

        const float textureRoughness = roughnessTexture.SampleLevel(textureSampler, surface.GetTextureCoordinates(), 0).r;
        Properties.Roughness *= textureRoughness * textureRoughness; // Square roughness
        Properties.Metallic *= metallicTexture.SampleLevel(textureSampler, surface.GetTextureCoordinates(), 0).r;
        Properties.EmissiveColor *= emissiveTexture.SampleLevel(textureSampler, surface.GetTextureCoordinates(), 0).rgb;

        m_ReflectionLookupTable = lookupTableReflection;
        m_RefractionLookupTableHitFromOutside = lookupTableRefractionHitFromOutside;
        m_RefractionLookupTableHitFromInside = lookupTableRefractionHitFromInside;
        m_Sampler = textureSampler;
    }

    // -----------------------------------------------------------------------------------------------------------------
    // BxDF Functions
    //
    //
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

    public BxDFSample SampleMetallic(inout Sampler sampler, in float3 V)
    {
        float3 H = sampler.GGXSampleAnisotopic(V, Ax, Ay);
        float3 L = normalize(reflect(-V, H));

        BxDFEval eval = EvaluateMetallic(V, L);

        return { L, eval.BxDF, eval.PDF };
    }

    public BxDFEval EvaluateMetallic(in float3 V, in float3 L)
    {
        float PDF;
        float3 H = normalize(V + L);

        float schlickWeight = SchlickWeight(dot(V, H));
        float3 F = lerp(Properties.BaseColor.xyz, 1.0f, schlickWeight);

        BxDFEval eval = EvaluateReflection(V, L, F);

        float layer = Properties.Anisotropy * 32.0;
        float energyCompensation = m_ReflectionLookupTable.SampleLevel(m_Sampler, {V.z, Properties.Roughness, layer}, 0).r;
        energyCompensation = (1.0f - energyCompensation) / energyCompensation;
        eval.BxDF = (1.0 + F * float3(energyCompensation)) * eval.BxDF;

        return eval;
    }

    public BxDFSample SampleDielectric(inout Sampler sampler, in float3 V)
    {
        float3 H = sampler.GGXSampleAnisotopic(V, Ax, Ay);
        float3 L = normalize(reflect(-V, H));

        BxDFEval eval = EvaluateDielectric(V, L);

        return { L, eval.BxDF, eval.PDF };
    }

    public BxDFEval EvaluateDielectric(in float3 V, in float3 L)
    {
        BxDFEval eval = EvaluateReflection(V, L, 1.0f);
        
        float layer = Properties.Anisotropy * 32.0;
        float energyCompensation = m_ReflectionLookupTable.SampleLevel(m_Sampler, {V.z, Properties.Roughness, layer}, 0).r;
        energyCompensation = (1.0f - energyCompensation) / energyCompensation;
        eval.BxDF = (1.0 + float3(energyCompensation)) * eval.BxDF;

        return eval;
    }

    public BxDFSample SampleGlass(inout Sampler sampler, in float3 V, out bool refracted)
    {
        float3 H = sampler.GGXSampleAnisotopic(V, Ax, Ay);

        float F = DielectricFresnel(abs(dot(V, H)));

        float x1 = sampler.UniformFloat();

        BxDFEval eval;
        float3 L;
        if (x1 < F)
        {
            // Reflect
            refracted = false;

            L = normalize(reflect(-V, H));

            eval = EvaluateReflection(V, L, F);
            eval.PDF *= F;
        }
        else
        {
            // Refract
            refracted = true;

            L = normalize(refract(-V, H, Eta));

            eval = EvaluateRefraction(V, L, F);
            eval.PDF *= (1.0f - F);
        }

        float energyCompensation;
        float layer = (clamp(Properties.IOR, 1.0001, 2.0) - 1.0f) * 32.0f;
        const bool isInside = NonUniformResourceIndex(Eta) > 1.0f;
        if (isInside)
        {
            energyCompensation = m_RefractionLookupTableHitFromInside.SampleLevel(m_Sampler, {pow(V.z, 1.0f / 2.0f), Properties.Roughness, layer}, 0).r;
        }
        else
        {
            energyCompensation = m_RefractionLookupTableHitFromOutside.SampleLevel(m_Sampler, {pow(V.z, 1.0f / 2.0f), Properties.Roughness, layer}, 0).r;
        }

        energyCompensation = max(energyCompensation, 0.00001);
        eval.BxDF = eval.BxDF / energyCompensation;

        return { L, eval.BxDF, eval.PDF };
    }

    //
    // References
    // [https://www.graphics.cornell.edu/~bjw/microfacetbsdf.pdf]
    // [https://inria.hal.science/hal-00996995v2/document]
    // [https://dl.acm.org/doi/pdf/10.1145/357290.357293]
    //
    public BxDFEval EvaluateReflection(in float3 V, in float3 L, in float3 F)
    {
        float3 H = normalize(V + L);

        float LdotH = max(0.0f, dot(L, H));
        float VdotH = max(0.0f, dot(V, H));

        float D = GGXDistributionAnisotropic(H);

        float GV = GGXSmithAnisotropic(V);
        float GL = GGXSmithAnisotropic(L);
        float G = GV * GL;

        float PDF = (GV * VdotH * D / V.z) / (4.0f * VdotH);
        float3 brdf = D * F * GV * GL / (4.0f * V.z);

        return { brdf, PDF };
    }

    //
    // References
    // [https://www.graphics.cornell.edu/~bjw/microfacetbsdf.pdf]
    // [https://inria.hal.science/hal-00996995v2/document]
    // [https://dl.acm.org/doi/pdf/10.1145/357290.357293]
    //
    public BxDFEval EvaluateRefraction(in float3 V, in float3 L, in float3 F)
    {
        float3 H = normalize(V + L);

        float VdotH = abs(dot(V, H));
        float LdotH = abs(dot(L, H));

        float D = GGXDistributionAnisotropic(H);
        float GV = GGXSmithAnisotropic(V);
        float GL = GGXSmithAnisotropic(L);
        float G = GV * GL;

        float denominator = (LdotH + Eta * VdotH);
        float denominator2 = denominator * denominator;
        float eta2 = Eta * Eta;

        float jacobian = (eta2 * LdotH) / denominator2;

        float PDF = (GV * VdotH * D / V.z) * jacobian;
        float3 bsdf = (Properties.BaseColor * (1.0f - F) * D * G * eta2 / denominator2) * (VdotH * LdotH / abs(V.z));

        return { bsdf, PDF };
    }

    // -----------------------------------------------------------------------------------------------------------------
    // GGX
    //
    // Based on Sampling the GGX Distribution of Visible Normals 2018 https://jcgt.org/published/0007/04/01/paper.pdf

    float GGXDistributionAnisotropic(float3 H)
    {
        float Hx2 = H.x * H.x;
        float Hy2 = H.y * H.y;
        float Hz2 = H.z * H.z;

        float ax2 = Ax * Ax;
        float ay2 = Ay * Ay;

        return 1.0f / (M_PI * Ax * Ay * pow(Hx2 / ax2 + Hy2 / ay2 + Hz2, 2.0f));
    }

    float Lambda(float3 V)
    {
        float Vx2 = V.x * V.x;
        float Vy2 = V.y * V.y;
        float Vz2 = abs(V.z) * abs(V.z);

        float ax2 = Ax * Ax;
        float ay2 = Ay * Ay;

        float nominator = -1.0f + sqrt(1.0f + (ax2 * Vx2 + ay2 * Vy2) / Vz2);

        return nominator / 2.0f;
    }

    float GGXSmithAnisotropic(float3 V)
    {
        return 1.0f / (1.0f + Lambda(V));
    }

    // -----------------------------------------------------------------------------------------------------------------

    float SchlickWeight(float VdotH)
    {
        float m = clamp(1.0 - VdotH, 0.0, 1.0);
        float m2 = m * m;
        return m2 * m2 * m;
    }

    public float DielectricFresnel(float VdotH)
    {
        float cosThetaI = VdotH;
        float sinThetaTSq = Eta * Eta * (1.0f - cosThetaI * cosThetaI);

        // Total internal reflection
        if (sinThetaTSq > 1.0)
            return 1.0;

        float cosThetaT = sqrt(max(1.0 - sinThetaTSq, 0.0));

        float rs = (Eta * cosThetaT - cosThetaI) / (Eta * cosThetaT + cosThetaI);
        float rp = (Eta * cosThetaI - cosThetaT) / (Eta * cosThetaI + cosThetaT);

        return 0.5f * (rs * rs + rp * rp);
    }
}