module Material;

import Sampler;
import rtcommon;

[[vk::binding(9, 0)]] Sampler2DArray uReflectionEnergyLookupTexture;

public struct BSDFSample
{
    public float3 L;
    public float3 BSDF;
    public float PDF;
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

    public float eta;

    public float ax;
    public float ay;

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

    [mutating]
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
        float layer = ((Anisotropy + 1.0f) / 2.0f) * 32.0f;
        float energyCompensation = uReflectionEnergyLookupTexture.SampleLevel( { V.z, Roughness, layer }, 0).r;
        BSDF = (1.0f + F * float3(energyCompensation)) * BSDF;

        return BSDF;
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

    // -----------------------------------------------------------------------------------------------------------------
    // GGX
    //
    // Based on Sampling the GGX Distribution of Visible Normals 2018 https://jcgt.org/published/0007/04/01/paper.pdf

    float GGXDistributionAnisotropic(float3 H)
    {
        float Hx2 = H.x * H.x;
        float Hy2 = H.y * H.y;
        float Hz2 = H.z * H.z;

        float ax2 = ax * ax;
        float ay2 = ay * ay;

        return 1.0f / (M_PI * ax * ay * pow(Hx2 / ax2 + Hy2 / ay2 + Hz2, 2.0f));
    }

    float Lambda(float3 V)
    {
        float Vx2 = V.x * V.x;
        float Vy2 = V.y * V.y;
        float Vz2 = abs(V.z) * abs(V.z);

        float ax2 = ax * ax;
        float ay2 = ay * ay;

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

        float3 Vh = normalize(float3(ax * Ve.x, ay * Ve.y, abs(Ve.z)));

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

        float3 Ne = normalize(float3(ax * Nh.x, ay * Nh.y, max(0.0, Nh.z)));

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