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
        // Roughness = Roughness^4 for better feel, linear roughness sucks
        Roughness = material.Roughness * material.Roughness * material.Roughness * material.Roughness;
        SpecularTint = material.SpecularTint;

        Ior = max(material.Ior, 1.000001);
        Transparency = material.Transparency;
        MediumDensity = material.MediumDensity;
        MediumAnisotropy = material.MediumAnisotropy;

        Anisotropy = material.Anisotropy;
        AnisotropyRotation = material.AnisotropyRotation / 360.0f * M_PI;

        const float aspect = sqrt(1.0 - sqrt(Anisotropy) * 0.9);
        Ax = max(0.001, Roughness / aspect);
        Ay = max(0.001, Roughness * aspect);

        Eta = dot(WorldRayDirection(), surface.GetGeometryNormal()) < 0.0 ? (1.0 / Ior) : Ior;

#ifdef FURNACE_TEST_MODE
        Color = float4(1.0f);
        EmissiveColor = float4(0.0f);
#else
        Color.rgb *= albedoTexture.SampleLevel(surface.GetTexCoords(), 0).rgb;
#endif

        Roughness *= roughnessTexture.SampleLevel(surface.GetTexCoords(), 0).r;
        Metallic *= metallicTexture.SampleLevel(surface.GetTexCoords(), 0).r;
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
        float3 H = GGXSampleAnisotopic(V, sampler);
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
        float3 H = GGXSampleAnisotopic(V, sampler);
        float3 L = normalize(reflect(-V, H));

        float PDF;
        float3 BSDF = EvaluateDielectricReflection(V, L, 1.0f, PDF);

        return { L, BSDF, PDF };
    }

    public float3 EvaluateDielectricReflection(in float3 V, in float3 L, in float F, out float PDF)
    {
        float3 H = normalize(V + L);

        float3 BSDF = EvaluateReflection(V, L, F, PDF);

        // Energy Compensation
        float layer = Anisotropy * 32.0f;
        float energyCompensation = uReflectionEnergyLookupTexture.SampleLevel( { V.z, Roughness, layer }, 0.0).r;
        energyCompensation = (1.0f - energyCompensation) / energyCompensation;
        BSDF = (1.0f + F * float3(energyCompensation)) * BSDF;

        PDF *= F;
        return BSDF;
    }

    public float3 EvaluateDielectricRefraction(in float3 V, in float3 L, in float F, out float PDF)
    {
        float3 BSDF = EvaluateRefraction(V, L, F, PDF);

        // Energy Compensation
        float energyCompensation;
        float renormalizedRoughness = pow(Roughness, 1.0f / 4.0f);
        float layer = pow((Ior - 1.0f) / 2.0f, 1.0f / 4.0f) * 63.0f;
        float layerFrac = fract(layer);
        if (Eta > 1.0f)
        {
            float energyCompensation1 = uRefractionEnergyLookupTextureEtaGreaterThan1 .Load( { int(pow(V.z, 1.0f / 2.5f) * 255.0f), int(renormalizedRoughness * 63.0f), int(floor(layer)) }) .r;
            float energyCompensation2 = uRefractionEnergyLookupTextureEtaGreaterThan1 .Load( { int(pow(V.z, 1.0f / 2.5f) * 255.0f), int(renormalizedRoughness * 63.0f), int(floor(layer) + 1) }).r;
            
            energyCompensation = lerp(energyCompensation1, energyCompensation2, layerFrac);
            //energyCompensation = uRefractionEnergyLookupTextureEtaGreaterThan1.SampleLevel( { pow(V.z, 1.0f / 2.5f), Roughness, floor(layer) }, 0.0).r;
        }
        else
        {
            float energyCompensation1 = uRefractionEnergyLookupTextureEtaLessThan1 .Load( { int(pow(V.z, 1.0f / 2.5f) * 255.0f), int(renormalizedRoughness * 63.0f), int(floor(layer)) }) .r;
            float energyCompensation2 = uRefractionEnergyLookupTextureEtaLessThan1 .Load( { int(pow(V.z, 1.0f / 2.5f) * 255.0f), int(renormalizedRoughness * 63.0f), int(floor(layer) + 1) }) .r;
            
            energyCompensation = lerp(energyCompensation1, energyCompensation2, layerFrac);
            //energyCompensation = uRefractionEnergyLookupTextureEtaLessThan1 .SampleLevel( { pow(V.z, 1.0f / 2.5f), Roughness, floor(layer) }, 0.0).r;
        }
        
        PDF *= (1.0f - F);

        // I have no clue why but glass is really fucking unstable and produces a lot of nans with eta > 1 which causes some values
        // in lookup tables to be really small and when BSDF is divided by something really small it becomes really big.
        // And because of that there are a lot of random energy gains which the code below tries to avoid by clamping energyCompensation Values.
        // No idea how to fix them properly since none of the papers even mentions this problem. I've spent a couple of days 
        // trying to find the issue but I had no luck so here goes a cheat fix. It's not perfect but I guess better this 
        // than nothing. After playing a bit with different values that's the best fit I could find.
        if (renormalizedRoughness >= 0.0f && renormalizedRoughness < 0.4f && energyCompensation != 0 && energyCompensation < 0.9f)
            energyCompensation = 0.0f;
        else if (renormalizedRoughness >= 0.4f && renormalizedRoughness < 0.7f && energyCompensation != 0 && energyCompensation < 0.9f)
            energyCompensation = 0.9f;
        else if (renormalizedRoughness >= 0.7f && renormalizedRoughness < 0.75f && energyCompensation != 0 && energyCompensation < 0.8f)
            energyCompensation = 0.8f;
        else if (renormalizedRoughness >= 0.75f && renormalizedRoughness < 0.8f && energyCompensation != 0 && energyCompensation < 0.7f)
            energyCompensation = 0.7f;
        else if (renormalizedRoughness >= 0.8f && renormalizedRoughness < 0.9f && energyCompensation != 0 && energyCompensation < 0.65f)
            energyCompensation = 0.6f;
        else if (renormalizedRoughness >= 0.9f && renormalizedRoughness < 0.95f && energyCompensation != 0 && energyCompensation < 0.4f)
            energyCompensation = 0.4f;
        else if (renormalizedRoughness >= 0.95f && renormalizedRoughness < 0.97f && energyCompensation != 0 && energyCompensation < 0.3f)
            energyCompensation = 0.3f;
        else if (renormalizedRoughness >= 0.97f && renormalizedRoughness <= 1.0f && energyCompensation != 0 && energyCompensation < 0.2f)
            energyCompensation = 0.2f;

        if (energyCompensation > 0)
        {
            energyCompensation = (1.0f - energyCompensation) / energyCompensation;
            BSDF = (1.0f + (1.0f - F) * float3(energyCompensation)) * BSDF;
        }

        //if (energyCompensation > 0)
        //    BSDF = BSDF / energyCompensation;

        return BSDF;
    }

    public BSDFSample SampleGlass(inout Sampler sampler, in float3 V, out bool refracted)
    {
        float3 H = GGXSampleAnisotopic(V, sampler);

        float F = DielectricFresnel(abs(dot(V, H)), Eta);

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
        }
        else
        {
            // Refract
            refracted = true;

            L = normalize(refract(-V, H, Eta));

            if (isnan(L.x) || isnan(L.y) || isnan(L.z) || isinf(L.x) || isinf(L.y) || isinf(L.z))
                return { 0, 0, 0 };

            BSDF = EvaluateDielectricRefraction(V, L, F, PDF);
        }

        return { L, BSDF, PDF };
    }

    float3 EvaluateReflection(in float3 V, in float3 L, in float3 F, out float PDF)
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

    float3 EvaluateRefraction(in float3 V, in float3 L, in float3 F, out float PDF)
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

    float DielectricFresnel(float VdotH, float eta)
    {
        float cosThetaI = VdotH;
        float sinThetaTSq = eta * eta * (1.0f - cosThetaI * cosThetaI);

        // Total internal reflection
        if (sinThetaTSq > 1.0)
            return 1.0;

        float cosThetaT = sqrt(max(1.0 - sinThetaTSq, 0.0));

        float rs = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);
        float rp = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);

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

    float3 GGXSampleAnisotopic(float3 Ve, inout Sampler sampler)
    {
        float u1 = sampler.UniformFloat();
        float u2 = sampler.UniformFloat();

        float3 Vh = normalize(float3(Ax * Ve.x, Ay * Ve.y, abs(Ve.z)));

        float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
        float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * (1.0f / sqrt(lensq)) : float3(1, 0, 0);
        float3 T2 = cross(Vh, T1);

        float r = sqrt(u1);
        float phi = 2.0 * M_PI * u2;
        float t1 = r * cos(phi);
        float t2 = r * sin(phi);
        float s = 0.5 * (1.0 + Vh.z);
        t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

        float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

        float3 Ne = normalize(float3(Ax * Nh.x, Ay * Nh.y, max(0.0, Nh.z)));

        return Ne;
    }

    // -----------------------------------------------------------------------------------------------------------------

    float SchlickWeight(float VdotH)
    {
        float m = clamp(1.0 - VdotH, 0.0, 1.0);
        float m2 = m * m;
        return m2 * m2 * m;
    }
};