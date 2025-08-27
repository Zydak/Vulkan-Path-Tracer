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
    public float3 SpecularColor;
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
    SamplerState m_LookupTableSampler;

    [mutating]
    public void Initialize(
        in CPUMaterial materialData,
        in Surface surface,
        in SamplerState textureSampler,
        in SamplerState lookupTableSampler,
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

        Properties.IOR = max(Properties.IOR, 1.000001f);

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
        
        // Textures

        Properties.BaseColor *= baseColorTexture.SampleLevel(textureSampler, surface.GetTextureCoordinates(), 0).rgb;

        const float textureRoughness = roughnessTexture.SampleLevel(textureSampler, surface.GetTextureCoordinates(), 0).r;
        Properties.Roughness *= textureRoughness * textureRoughness; // Square roughness
        Properties.Metallic *= metallicTexture.SampleLevel(textureSampler, surface.GetTextureCoordinates(), 0).r;
        Properties.EmissiveColor *= emissiveTexture.SampleLevel(textureSampler, surface.GetTextureCoordinates(), 0).rgb;

        m_ReflectionLookupTable = lookupTableReflection;
        m_RefractionLookupTableHitFromOutside = lookupTableRefractionHitFromOutside;
        m_RefractionLookupTableHitFromInside = lookupTableRefractionHitFromInside;
        m_LookupTableSampler = lookupTableSampler;

        const float aspect = sqrt(1.0 - sqrt(Properties.Anisotropy) * 0.9);
        Ax = max(0.001, Properties.Roughness / aspect);
        Ay = max(0.001, Properties.Roughness * aspect);
    }

    // -----------------------------------------------------------------------------------------------------------------
    // BxDF Functions
    //
    //

    public BxDFSample SampleBSDF(inout Sampler sampler, in float3 V, in float3 H, in float FDielectric)
    {
        // Compute sampling probabilities
        float metallicProbability = Properties.Metallic;
        float dielectricProbability = (1.0 - Properties.Metallic) * (1.0f - Properties.Transmission);
        float glassProbability = (1.0 - Properties.Metallic) * Properties.Transmission;

        // Normalize probabilities so they sum up to 1
        float probabilitySum = metallicProbability + dielectricProbability + glassProbability;
        metallicProbability /= probabilitySum;
        dielectricProbability /= probabilitySum;
        glassProbability /= probabilitySum;

        float x1 = sampler.UniformFloat();

        float3 L;
        if (x1 < metallicProbability)
        {
            // Sample metallic lobe
            L = normalize(reflect(-V, H));
        }
        else if (x1 < metallicProbability + dielectricProbability)
        {
            // Sample dielectric lobe
            if (sampler.UniformFloat() < FDielectric)
            {
                // Reflect
                L = normalize(reflect(-V, H));
            }
            else
            {
                // Scatter diffusely
                L = sampler.RandomHemisphereVecCosineWeight(float3(0.0f, 0.0f, 1.0f));
            }
        }
        else
        {
            if (sampler.UniformFloat() < FDielectric)
            {
                // Reflect
                L = normalize(reflect(-V, H));
            }
            else
            {
                // Refract
                L = normalize(refract(-V, H, Eta));
            }
        }

        BxDFEval scatteringEval = EvaluateBSDF(V, H, L, FDielectric);
        return { L, scatteringEval.BxDF, scatteringEval.PDF };
    }

    public BxDFEval EvaluateBSDF(in float3 V, in float3 H, in float3 L, in float FDielectric)
    {
        // Compute sampling probabilities
        float metallicProbability = Properties.Metallic;
        float dielectricProbability = (1.0 - Properties.Metallic) * (1.0f - Properties.Transmission);
        float glassProbability = (1.0 - Properties.Metallic) * Properties.Transmission;

        // Normalize probabilities so they sum up to 1
        float probabilitySum = metallicProbability + dielectricProbability + glassProbability;
        metallicProbability /= probabilitySum;
        dielectricProbability /= probabilitySum;
        glassProbability /= probabilitySum;

        BxDFEval scatterDirectionEval = {0, 0};
        {
            float energyCompensation;
            #ifdef USE_ENERGY_COMPENSATION
            {
                const bool isInside = NonUniformResourceIndex(Eta) > 1.0f;
                float layer = (clamp(Properties.IOR, 1.0001, 2.0) - 1.0f) * 32.0f;
                if (isInside)
                {
                    energyCompensation = m_RefractionLookupTableHitFromInside.SampleLevel(m_LookupTableSampler, {pow(V.z, 1.0f / 2.0f), Properties.Roughness, layer}, 0).r;
                }
                else
                {
                    energyCompensation = m_RefractionLookupTableHitFromOutside.SampleLevel(m_LookupTableSampler, {pow(V.z, 1.0f / 2.0f), Properties.Roughness, layer}, 0).r;
                }
            }
            #endif

            // Metallic
            {
                BxDFEval metallicEval = EvaluateMetallic(V, H, L);
                scatterDirectionEval.BxDF += metallicEval.BxDF * metallicProbability;
                scatterDirectionEval.PDF += metallicEval.PDF * metallicProbability;
            }
            // Diffuse
            {
                BxDFEval diffuseEval = EvaluateDiffuse(V, H, L);
                scatterDirectionEval.BxDF += diffuseEval.BxDF * dielectricProbability * (1.0f - FDielectric);
                scatterDirectionEval.PDF += diffuseEval.PDF * dielectricProbability * (1.0f - FDielectric);
            }
            // Specular Dielectric
            {
                BxDFEval specularEval = EvaluateDielectricReflection(V, H, L);
                scatterDirectionEval.BxDF += specularEval.BxDF * dielectricProbability * FDielectric;
                scatterDirectionEval.PDF += specularEval.PDF * dielectricProbability * FDielectric;
            }
            // Glass Reflect
            {
                BxDFEval glassEval = EvaluateReflection(V, L, Properties.SpecularColor);

                #ifdef USE_ENERGY_COMPENSATION
                if (energyCompensation > 0.01f)
                {
                    glassEval.BxDF /= energyCompensation;
                }
                #endif

                scatterDirectionEval.BxDF += glassEval.BxDF * glassProbability * FDielectric;
                scatterDirectionEval.PDF += glassEval.PDF * glassProbability * FDielectric;
            }
            // Glass Refract
            {
                BxDFEval glassEval = EvaluateRefraction(V, L, Properties.BaseColor);

                #ifdef USE_ENERGY_COMPENSATION
                if (energyCompensation > 0.01f)
                {
                    glassEval.BxDF /= energyCompensation;
                }
                #endif

                scatterDirectionEval.BxDF += glassEval.BxDF * glassProbability * (1.0f - FDielectric);
                scatterDirectionEval.PDF += glassEval.PDF * glassProbability * (1.0f - FDielectric);
            }
        }
        return scatterDirectionEval;
    }

    public BxDFEval EvaluateDiffuse(in float3 V, in float3 H, in float3 L)
    {
        float PDF = L.z * M_1_OVER_PI;
        float3 BRDF = M_1_OVER_PI * Properties.BaseColor * L.z;

        PDF *= (float)(L.z > 0.0f);

        return { BRDF, PDF };
    }

    public BxDFEval EvaluateMetallic(in float3 V, in float3 H, in float3 L)
    {
        float3 F = lerp(Properties.BaseColor, Properties.SpecularColor, SchlickFresnel(dot(V, H)));

        BxDFEval eval = EvaluateReflection(V, L, F);

        #ifdef USE_ENERGY_COMPENSATION
        {
            float layer = Properties.Anisotropy * 32.0;
            float energyCompensation = m_ReflectionLookupTable.SampleLevel(m_LookupTableSampler, {V.z, Properties.Roughness, layer}, 0).r;
            energyCompensation = (1.0f - energyCompensation) / energyCompensation;
            eval.BxDF = (1.0 + F * float3(energyCompensation)) * eval.BxDF;
        }
        #endif

        return eval;
    }

    public BxDFEval EvaluateDielectricReflection(in float3 V, in float3 H, in float3 L)
    {
        BxDFEval eval = EvaluateReflection(V, L, Properties.SpecularColor);

        #ifdef USE_ENERGY_COMPENSATION
        {
            float layer = Properties.Anisotropy * 32.0;
            float energyCompensation = m_ReflectionLookupTable.SampleLevel(m_LookupTableSampler, {V.z, Properties.Roughness, layer}, 0).r;
            eval.BxDF /= energyCompensation;
        }
        #endif

        return eval;
    }

    //
    // References
    // [https://www.graphics.cornell.edu/~bjw/microfacetbsdf.pdf]
    // [https://inria.hal.science/hal-00996995v2/document]
    // [https://dl.acm.org/doi/pdf/10.1145/357290.357293]
    //
    public BxDFEval EvaluateReflection(in float3 V, in float3 L, in float3 F)
    {
        if (L.z <= 0.01f)
            return { float3(0.0f, 0.0f, 0.0f), 0.0f };
        
        float3 H = normalize(V + L);

        float LdotH = dot(L, H);
        float VdotH = dot(V, H);

        float D = GGXDistributionAnisotropic(H);

        float GV = GGXSmithAnisotropic(V);
        float GL = GGXSmithAnisotropic(L);
        float G = GV * GL;

        float PDF = (GV * max(VdotH, 0.0f) * D / V.z) / (4.0f * VdotH);
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
        if (L.z >= 0.0f)
            return { float3(0.0f, 0.0f, 0.0f), 0.0f };

        float3 H = normalize(Eta * V + L);

        if (H.z < 0.0f)
            H = -H;

        float VdotH = dot(V, H);
        float LdotH = dot(L, H);

        float D = GGXDistributionAnisotropic(H);
        float GV = GGXSmithAnisotropic(V);
        float GL = GGXSmithAnisotropic(L);
        float G = GV * GL;

        float denominator = (LdotH + Eta * VdotH);
        float denominator2 = denominator * denominator;
        float eta2 = Eta * Eta;

        float jacobian = (eta2 * abs(LdotH)) / denominator2;

        float PDF = (GV * abs(VdotH) * D / V.z) * jacobian;
        float3 bsdf = (F * D * G * eta2 / denominator2) * (abs(VdotH) * abs(LdotH) / (abs(V.z)));

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

    public float SchlickFresnel(float VdotH)
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