#ifndef BSDFFile
#define BSDFFile

#include "raycommon.glsl"
#include "Sampling.glsl"

struct BSDFSampleData
{
    vec3  View;         // [in] Toward the incoming ray
    vec3  RayDir;       // [out] Reflect Dir
    float PDF;          // [out] PDF
    vec3  BSDF;         // [out] BSDF
};

struct BSDFEvaluateData
{
    vec3  View;         // [in] Toward the incoming ray
    vec3  RayDir;       // [in] Reflect Dir
    float PDF;          // [out] PDF
    vec3  BSDF;         // [out] BSDF
};

vec3 Fresnel(float LdotH, vec3 cspec)
{
    return cspec + (1.0f - cspec) * pow(1.0f - LdotH, 5.0f);
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

float SchlickWeight(float VdotH)
{
    float m = clamp(1.0 - VdotH, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;
}

vec3 EvalDiffuse(in Material mat, vec3 L, out float pdf)
{
    // BRDF = Albedo / M_PI * NdotL
    // PDF = NdotL / M_PI
    // 
    // Fr = BRDF / PDF
    // 
    // If we expand it we get
    // 
    //      Albedo * NdotL * M_PI
    // Fr = ---------------------
    //          M_PI * NdotL
    //
    // So we're only left with Fr = Albedo.

    //pdf = L.z * M_1_OVER_PI;
    //vec3 bsdf = M_1_OVER_PI * mat.Color.xyz * L.z;

    pdf = 1.0f;
    vec3 bsdf = mat.Color.xyz;

    return bsdf;
}

vec3 EvalMetal(in Material mat, vec3 L, vec3 V, vec3 H, out float pdf)
{
    // BRDF = D * F * GV * GL / (4.0f * NdotV * NdotL) * NdotL
    // 
    // PDF is VNDF / jacobian of reflect()
    // PDF = (GV * VdotH * D / NdotV) / (4.0f * VdotH)
    //
    // Fr = BRDF / PDF
    //
    // If we expand it we get
    // 
    //      D * F * GV * GL * 4.0f * NdotV * NdotL * VdotH
    // Fr = ----------------------------------------------
    //          4.0f * NdotL * VdotH * NdotV * GV * D
    //
    // almost everything cancels out and we're only left with F * GL. Noice.

    float LdotH = max(0.0f, dot(L, H));
    float VdotH = max(0.0f, dot(V, H));

    float D = GGXDistributionAnisotropic(H, mat.ax, mat.ay);
    vec3 F = mix(mat.Color.xyz, vec3(1.0f), SchlickWeight(VdotH));

    float GV = GGXSmithAnisotropic(V, mat.ax, mat.ay);
    float GL = GGXSmithAnisotropic(L, mat.ax, mat.ay);
    float G = GV * GL;

    pdf = 1.0f;
    vec3 bsdf = F * GL;

    //pdf = (GV * VdotH * D / V.z) / (4.0f * VdotH);
    //vec3 bsdf = D * F * GV * GL / (4.0f * V.z);

    // Lookup table for energy compensation
    float energyCompensation = texture(uEnergyLookupTexture, vec2(V.z, mat.Roughness)).r;

    //data.BSDF += vec3(energyCompensation) * bsdf + bsdf * metallicProbability; // No fresnel
    bsdf = (1.0f + F * vec3(energyCompensation)) * bsdf; // Fresnel

    return bsdf;
}

vec3 EvalDielectricReflection(in Material mat, vec3 L, vec3 V, vec3 H, vec3 F, out float pdf)
{
    // BRDF = D * F * GV * GL / (4.0f * NdotV * NdotL) * NdotL
    // 
    // PDF is VNDF * jacobian of reflect()
    // PDF = (GV * VdotH * D / NdotV) / (4.0f * VdotH)
    //
    // Fr = BRDF / PDF
    //
    // If we expand it we get
    // 
    //      D * F * GV * GL * 4.0f * NdotV * NdotL * VdotH
    // Fr = ----------------------------------------------
    //          4.0f * NdotL * VdotH * NdotV * GV * D
    //
    // almost everything cancels out and we're only left with F * GL. Noice.

    float LdotH = max(0.0f, dot(L, H));
    float VdotH = max(0.0f, dot(V, H));

    float D = GGXDistributionAnisotropic(H, mat.ax, mat.ay);

    float GV = GGXSmithAnisotropic(V, mat.ax, mat.ay);
    float GL = GGXSmithAnisotropic(L, mat.ax, mat.ay);
    float G = GV * GL;

    pdf = 1.0f;
    vec3 bsdf = F * GL;

    //pdf = (GV * VdotH * D / V.z) / (4.0f * VdotH);
    //vec3 bsdf = D * F * GV * GL / (4.0f * V.z);

    // Lookup table for energy compensation
    float energyCompensation = texture(uEnergyLookupTexture, vec2(V.z, mat.Roughness)).r;

    //data.BSDF += vec3(energyCompensation) * bsdf + bsdf * metallicProbability; // No fresnel
    bsdf = (1.0f + F * vec3(energyCompensation)) * bsdf; // Fresnel

    return bsdf;
}

vec3 EvalDielectricRefraction(in Material mat, in Surface surface, vec3 L, vec3 V, vec3 H, vec3 F, out float pdf)
{
    if (L.z >= 0.0f)
        return vec3(0.0f);

    // BRDF = D * F * GV * GL / (4.0f * NdotV * NdotL) * NdotL
    // 
    // PDF is VNDF * jacobian of refract()
    // PDF = (GV * VdotH * D / NdotV) * (eta * eta * abs(LdotH)) / pow(VdotH + eta * abs(LdotH), 2)
    //
    // Fr = BRDF / PDF

    float LdotH = dot(L, H);
    float VdotH = dot(V, H);

    float D = GGXDistributionAnisotropic(H, mat.ax, mat.ay);

    float GV = GGXSmithAnisotropic(V, mat.ax, mat.ay);
    float GL = GGXSmithAnisotropic(L, mat.ax, mat.ay);
    float G = GV * GL;

    float denominator = (LdotH + mat.eta * VdotH);
    float eta2 = mat.eta * mat.eta;

    pdf = (GV * max(VdotH, 0.0f) * D / V.z) * (eta2 / (denominator * denominator));
    vec3 bsdf = eta2 * (vec3(1.0f) - F) * G * D / (denominator * denominator);

    //// Lookup table for energy compensation
    //float energyCompensation = texture(uEnergyLookupTexture, vec2(V.z, mat.Roughness)).r;
    //
    //data.BSDF += vec3(energyCompensation) * bsdf + bsdf * metallicProbability; // No fresnel
    //bsdf = (1.0f + F * vec3(energyCompensation)) * bsdf; // Fresnel

    return bsdf;
}

bool SampleBSDF(inout uint seed, inout BSDFSampleData data, in Material mat, in Surface surface)
{
    vec3 V = WorldToTangent(surface.Tangent, surface.Bitangent, surface.Normal, data.View);

    float F0 = (1.0f - mat.eta) / (1.0f + mat.eta);
    F0 *= F0;

    float schlickWt = SchlickWeight(V.z);

    float diffuseProbability = (1.0f - mat.Metallic) * (1.0f - mat.Transparency);
    float metallicProbability = mat.Metallic;
    float dieletricProbability = (1.0f - mat.Metallic) * F0 * (1.0f - mat.Transparency);
    float glassProbability = (1.0f - mat.Metallic) * mat.Transparency;

    float inverseTotalProbability = 1.0f / (diffuseProbability + metallicProbability + dieletricProbability + glassProbability);

    diffuseProbability *= inverseTotalProbability;
    metallicProbability *= inverseTotalProbability;
    dieletricProbability *= inverseTotalProbability;
    glassProbability *= inverseTotalProbability;

    float diffuseCDF = diffuseProbability;
    float metallicCDF = diffuseCDF + metallicProbability;
    float dielectricCDF = metallicCDF + dieletricProbability;
    float glassCDF = dielectricCDF + glassProbability;

    float r1 = Rnd(seed);

    if (r1 < diffuseCDF)
    {
        // Diffuse
    
        data.RayDir = CosineSamplingHemisphere(seed, surface.Normal);
        vec3 L = WorldToTangent(surface.Tangent, surface.Bitangent, surface.Normal, data.RayDir);
        data.BSDF = EvalDiffuse(mat, L, data.PDF);
    }
    else if (r1 < metallicCDF)
    {
        // Metallic
    
        vec3 H = GGXSampleAnisotopic(V, mat.ax, mat.ay, Rnd(seed), Rnd(seed));
        data.RayDir = normalize(reflect(-V, H));

        if (data.RayDir.z < 0.0f)
            return false;

        data.BSDF = EvalMetal(mat, data.RayDir, V, H, data.PDF);
    
        data.RayDir = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, data.RayDir);
    }
    else if (r1 < dielectricCDF)
    {
        vec3 H = GGXSampleAnisotopic(V, mat.ax, mat.ay, Rnd(seed), Rnd(seed));
        data.RayDir = normalize(reflect(-V, H));

        if (data.RayDir.z < 0.0f)
            return false;
    
        data.BSDF = EvalDielectricReflection(mat, data.RayDir, V, H, vec3(1.0f), data.PDF);
    
        data.RayDir = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, data.RayDir);
    }
    else if (r1 < glassCDF)
    {
        vec3 H = GGXSampleAnisotopic(V, mat.ax, mat.ay, Rnd(seed), Rnd(seed));
        float F = DielectricFresnel(abs(dot(V, H)), mat.eta);

        float r2 = Rnd(seed);

        if (r2 < F)
        {
            // Reflect
            data.RayDir = normalize(reflect(-V, H));
            data.BSDF = EvalDielectricReflection(mat, data.RayDir, V, H, vec3(F), data.PDF);
        }
        else
        {
            data.RayDir = normalize(refract(-V, H, mat.eta));
            data.BSDF = EvalDielectricRefraction(mat, surface, data.RayDir, V, H, vec3(F), data.PDF);
        }

        data.RayDir = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, data.RayDir);
    }

    return true;
}

void EvaluateBSDF(inout BSDFEvaluateData data, in Material mat, in Surface surface)
{
    vec3 V = WorldToTangent(surface.Tangent, surface.Bitangent, surface.Normal, data.View);
    vec3 L = WorldToTangent(surface.Tangent, surface.Bitangent, surface.Normal, data.RayDir);
    vec3 H = normalize(V + L);

    float NdotH = max(H.z, 0.0f);
    float NdotV = max(V.z, 0.0f);
    float NdotL = max(L.z, 0.0f);
    float VdotH = max(dot(V, H), 0.0f);
    float LdotH = max(dot(L, H), 0.0f);

    float diffuseProbability = 1.0f - mat.Metallic;
    float metallicProbability = mat.Metallic;

    float inverseTotalProbability = 1.0f / (diffuseProbability + metallicProbability);

    diffuseProbability *= inverseTotalProbability;
    metallicProbability *= inverseTotalProbability;

    data.PDF = 0.0f;
    data.BSDF = vec3(0.0f);

    {
        // Diffuse
    
        float pdf;
        data.BSDF += EvalDiffuse(mat, L, pdf) * diffuseProbability;
        data.PDF += pdf* diffuseProbability;
    }
    {
        // Metallic
    
        float pdf;
        data.BSDF += EvalMetal(mat, L, V, H, pdf) * metallicProbability;
        data.PDF += pdf * metallicProbability;
    }
}

#endif