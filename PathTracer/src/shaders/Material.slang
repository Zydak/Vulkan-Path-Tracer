module Material;

import Sampler;
import rtcommon;
import Surface;

// 6 = "r32f"
[[vk::binding(9, 0)]] Sampler2DArray<float, 0, 6> uReflectionEnergyLookupTexture;
[[vk::binding(10, 0)]] Sampler2DArray<float, 0, 6> uRefractionEnergyLookupTextureEtaGreaterThan1;
[[vk::binding(11, 0)]] Sampler2DArray<float, 0, 6> uRefractionEnergyLookupTextureEtaLessThan1;

public struct BSDFSample
{
    public float3 L;
    public float3 BSDF;
    public float PDF;
}

// Material struct on the CPU side
public struct CPUMaterial
{
    public float4 Color;
    public float4 EmissiveColor;
    public float4 MediumColor;

    public float Metallic;
    public float Roughness;
    public float SpecularTint;

    public float Ior;
    public float Transparency;
    public float MediumDensity;
    public float MediumAnisotropy;

    public float Anisotropy;
    public float AnisotropyRotation;

    public float x1;
    public float x2;
    public float x3;
}

public struct Material
{
    public float4 Color;
    public float4 EmissiveColor;
    public float4 MediumColor;

    public float Metallic;
    public float Roughness;
    public float SpecularTint;

    public float Ior;
    public float Transparency;
    public float MediumDensity;
    public float MediumAnisotropy;

    public float Anisotropy;
    public float AnisotropyRotation;

    public float Eta;

    public float Ax;
    public float Ay;

    [mutating]
    public void LoadFromCPU(in CPUMaterial material, in Surface surface, in Sampler2D albedoTexture, in Sampler2D roughnessTexture, in Sampler2D metallicTexture)
    {
        Color = material.Color;
        EmissiveColor = material.EmissiveColor;
        MediumColor = material.MediumColor;

        Metallic = material.Metallic;
        // Roughness = Roughness^2 for better feel, linear roughness sucks
        Roughness = material.Roughness * material.Roughness;
        SpecularTint = material.SpecularTint;

        Ior = max(material.Ior, 1.000001);
        Transparency = material.Transparency;
        MediumDensity = material.MediumDensity;
        MediumAnisotropy = material.MediumAnisotropy;

        Anisotropy = material.Anisotropy;
        AnisotropyRotation = material.AnisotropyRotation / 360.0f * M_PI;

#ifdef FURNACE_TEST_MODE
        Color = float4(1.0f);
        EmissiveColor = float4(0.0f);
#else
        Color.rgb *= albedoTexture.SampleLevel(surface.GetTexCoords(), 0).rgb;
#endif

        Roughness *= pow(roughnessTexture.SampleLevel(surface.GetTexCoords(), 0).r, 1.0f / 4.0f);
        Metallic *= metallicTexture.SampleLevel(surface.GetTexCoords(), 0).r;

        // Some scenes handle transparency through albedo texture alpha channel. But since this is path tracer it cares only about the geometry
        // and not the color. So if transparency is detected (alpha channel < 1) set the texture to fully transparent glass with IOR 1 so it looks
        // like it's just not there. Though it will still block direct lighting from importance sampling lights.
        float texTrans = albedoTexture.SampleLevel(surface.GetTexCoords(), 0).a;
        if (texTrans < 1)
        {
            Roughness = 0;
            Metallic = 0;
            Color.rgb = 1.0f;
            Ior = 1.0001f;
            Transparency = 1.0f;
        }

        Eta = dot(WorldRayDirection(), surface.GetGeometryNormal()) < 0.0 ? (1.0 / Ior) : Ior;

        const float aspect = sqrt(1.0 - sqrt(Anisotropy) * 0.9);
        Ax = max(0.001, Roughness / aspect);
        Ay = max(0.001, Roughness * aspect);
    }

    public BSDFSample SampleDiffuse(inout Sampler sampler, in float3 V)
    {
        float3 L = sampler.RandomSphereVecCosineWeight(float3(0.0f, 0.0f, 1.0f)); // Tangent space

        float PDF;
        float3 BSDF = EvaluateDiffuse(V, L, PDF);

        return { L, BSDF, PDF };
    }

    public float3 EvaluateDiffuse(in float3 V, in float3 L, out float PDF)
    {
        PDF = L.z * M_1_OVER_PI;
        float3 BSDF = M_1_OVER_PI * Color.rgb * L.z;

        return BSDF;
    }

    public BSDFSample SampleMetallic(inout Sampler sampler, in float3 V)
    {
        float3 H = sampler.GGXSampleAnisotopic(V, Ax, Ay);
        float3 L = normalize(reflect(-V, H));

        float PDF;
        float3 BSDF = EvaluateMetallic(V, L, PDF);

        return { L, BSDF, PDF };
    }

    public float3 EvaluateMetallic(in float3 V, in float3 L, out float PDF)
    {
        float3 H = normalize(V + L);

        float schlickWeight = SchlickWeight(dot(V, H));
        float3 F = lerp(Color.xyz, 1.0f, schlickWeight);

        float3 BSDF = EvaluateReflection(V, L, F, PDF);

        // Energy Compensation
        float layer = Anisotropy * 32.0f;
        float energyCompensation = uReflectionEnergyLookupTexture.SampleLevel( { V.z, Roughness, layer }, 0.0).r;
        energyCompensation = (1.0f - energyCompensation) / energyCompensation;
        BSDF = (1.0f + F * float3(energyCompensation)) * BSDF;

        return BSDF;
    }

    public BSDFSample SampleDielectric(inout Sampler sampler, in float3 V)
    {
        float3 H = sampler.GGXSampleAnisotopic(V, Ax, Ay);
        float3 L = normalize(reflect(-V, H));

        float PDF;
        float3 BSDF = EvaluateDielectricReflection(V, L, 1.0f, PDF);

        float layer = Anisotropy * 32.0f;
        float energyCompensation = uReflectionEnergyLookupTexture.SampleLevel( { V.z, Roughness, layer }, 0.0).r;
        energyCompensation = (1.0f - energyCompensation) / energyCompensation;
        BSDF = (1.0f + float3(energyCompensation)) * BSDF;

        return { L, BSDF, PDF };
    }

    public float3 EvaluateDielectricReflection(in float3 V, in float3 L, in float F, out float PDF)
    {
        float3 BSDF = EvaluateReflection(V, L, F, PDF);

        return BSDF;
    }

    public float3 EvaluateDielectricRefraction(in float3 V, in float3 L, in float F, out float PDF)
    {
        float3 BSDF = EvaluateRefraction(V, L, F, PDF);

        return BSDF;
    }

    public BSDFSample SampleGlass(inout Sampler sampler, in float3 V, out bool refracted, out float F)
    {
        float3 H = sampler.GGXSampleAnisotopic(V, Ax, Ay);

        F = DielectricFresnel(abs(dot(V, H)));

        float x1 = sampler.UniformFloat();

        float PDF;
        float3 BSDF;
        float3 L;
        if (x1 < F)
        {
            // Reflect
            refracted = false;

            L = normalize(reflect(-V, H));

            BSDF = EvaluateDielectricReflection(V, L, F, PDF);
            PDF *= F;
        }
        else
        {
            // Refract
            refracted = true;

            L = normalize(refract(-V, H, Eta));

            BSDF = EvaluateDielectricRefraction(V, L, F, PDF);
            PDF *= (1.0f - F);
        }

        // Energy Compensation
        float energyCompensation;
        float layer = (clamp(Ior, 1.0, 3.0) - 1.0f) / 2.0f * 31.0f;
        float layerFrac = fract(layer);
        if (NonUniformResourceIndex(Eta) > 1.0f)
        {
            energyCompensation = uRefractionEnergyLookupTextureEtaGreaterThan1 .Load( { int(pow(V.z, 1.0f / 2.0f) * 127.0f), int(Roughness * 127.0f), int(layer) }) .r;
        }
        else
        {
            energyCompensation = uRefractionEnergyLookupTextureEtaLessThan1 .Load( { int(pow(V.z, 1.0f / 2.0f) * 127.0f), int(Roughness * 127.0f), int(layer) }) .r;
        }

        if (isnan(L.x) || isnan(L.y) || isnan(L.z) || isinf(L.x) || isinf(L.y) || isinf(BSDF.z))
            return { 0, 0, 0 };

        if (isnan(BSDF.x) || isnan(BSDF.y) || isnan(BSDF.z) || isinf(BSDF.x) || isinf(BSDF.y) || isinf(BSDF.z))
            return { 0, 0, 0 };

        if (!refracted && L.z <= 0)
            return { 0, 0, 0 };

        if (refracted && L.z >= 0)
            return { 0, 0, 0 };

        energyCompensation = max(energyCompensation, 0.00001);

        BSDF = BSDF / energyCompensation;

        return { L, BSDF, PDF };
    }

    //
    // References
    // [https://www.graphics.cornell.edu/~bjw/microfacetbsdf.pdf]
    // [https://inria.hal.science/hal-00996995v2/document]
    // [https://dl.acm.org/doi/pdf/10.1145/357290.357293]
    //
    public float3 EvaluateReflection(in float3 V, in float3 L, in float3 F, out float PDF)
    {
        float3 H = normalize(V + L);

        float LdotH = max(0.0f, dot(L, H));
        float VdotH = max(0.0f, dot(V, H));

        float D = GGXDistributionAnisotropic(H);

        float GV = GGXSmithAnisotropic(V);
        float GL = GGXSmithAnisotropic(L);
        float G = GV * GL;

        //PDF = 1.0f;
        //float3 bsdf = F * GL;

        PDF = (GV * VdotH * D / V.z) / (4.0f * VdotH);
        float3 bsdf = D * F * GV * GL / (4.0f * V.z);

        return bsdf;
    }

    //
    // References
    // [https://www.graphics.cornell.edu/~bjw/microfacetbsdf.pdf]
    // [https://inria.hal.science/hal-00996995v2/document]
    // [https://dl.acm.org/doi/pdf/10.1145/357290.357293]
    //
    public float3 EvaluateRefraction(in float3 V, in float3 L, in float3 F, out float PDF)
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

        PDF = (GV * VdotH * D / V.z) * jacobian;
        float3 bsdf = (Color.xyz * (1.0f - F) * D * G * eta2 / denominator2) * (VdotH * LdotH / abs(V.z));

        return bsdf;
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
};